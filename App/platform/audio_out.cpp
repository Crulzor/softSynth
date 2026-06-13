#include "audio_out.hpp"

extern "C" {
#include "main.h"
}

namespace {

// Double buffer: two halves of kBlockSize stereo (2-channel) frames each.
// Lives in RAM_D2 (0x30000000) because the DMA engines can't reach DTCM and
// this keeps it out of the way of the (future) LTDC framebuffer in AXI-SRAM.
// 32-byte aligned for cache-line friendliness if D-cache is enabled later.
constexpr uint32_t kFramesPerBuffer = audio_out::kBlockSize;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kHalfSamples = kFramesPerBuffer * kChannels;
constexpr uint32_t kTotalSamples = kHalfSamples * 2;

__attribute__((section(".dma_buffer"), aligned(32)))
int16_t g_buffer[kTotalSamples];

SAI_HandleTypeDef g_sai{};
DMA_HandleTypeDef g_dma{};
audio_out::RenderFn g_render = nullptr;

void clock_config() {
  // PLL2 from HSE (25 MHz): M=25 -> 1 MHz VCO in, N=344 -> 344 MHz VCO out,
  // P=7 -> 49.142 MHz SAI kernel clock. The SAI master divider then yields
  // ~12.288 MHz MCLK (256*48k) with only ~0.02% error vs the default -2.32%.
  RCC_PeriphCLKInitTypeDef periph{};
  HAL_RCCEx_GetPeriphCLKConfig(&periph);
  periph.PeriphClockSelection = RCC_PERIPHCLK_SAI2;
  periph.Sai23ClockSelection = RCC_SAI2CLKSOURCE_PLL2;
  periph.PLL2.PLL2M = 25;
  periph.PLL2.PLL2N = 344;
  periph.PLL2.PLL2P = 7;
  periph.PLL2.PLL2Q = 1;
  periph.PLL2.PLL2R = 1;
  periph.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;   // 1-2 MHz VCO input
  periph.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  periph.PLL2.PLL2FRACN = 0;
  HAL_RCCEx_PeriphCLKConfig(&periph);
}

void gpio_init() {
  __HAL_RCC_GPIOI_CLK_ENABLE();
  GPIO_InitTypeDef gpio{};
  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;  // MCLK/SCK/SD/FS
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_SAI2;
  HAL_GPIO_Init(GPIOI, &gpio);
}

void dma_init() {
  __HAL_RCC_DMA2_CLK_ENABLE();
  g_dma.Instance = DMA2_Stream1;
  g_dma.Init.Request = DMA_REQUEST_SAI2_A;
  g_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
  g_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  g_dma.Init.MemInc = DMA_MINC_ENABLE;
  g_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  g_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  g_dma.Init.Mode = DMA_CIRCULAR;
  g_dma.Init.Priority = DMA_PRIORITY_HIGH;
  g_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  HAL_DMA_Init(&g_dma);
  __HAL_LINKDMA(&g_sai, hdmatx, g_dma);

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
}

}  // namespace

namespace audio_out {

bool init() {
  clock_config();
  gpio_init();
  dma_init();

  __HAL_RCC_SAI2_CLK_ENABLE();
  g_sai.Instance = SAI2_Block_A;
  g_sai.Init.AudioMode = SAI_MODEMASTER_TX;
  g_sai.Init.Synchro = SAI_ASYNCHRONOUS;
  g_sai.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  g_sai.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  g_sai.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  g_sai.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  g_sai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  g_sai.Init.Mckdiv = 0;  // computed by HAL from AudioFrequency
  g_sai.Init.MonoStereoMode = SAI_STEREOMODE;
  g_sai.Init.CompandingMode = SAI_NOCOMPANDING;
  g_sai.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  g_sai.Init.Protocol = SAI_FREE_PROTOCOL;
  g_sai.Init.DataSize = SAI_DATASIZE_16;
  g_sai.Init.FirstBit = SAI_FIRSTBIT_MSB;
  g_sai.Init.ClockStrobing = SAI_CLOCKSTROBING_RISINGEDGE;
  g_sai.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  g_sai.Init.MckOutput = SAI_MCK_OUTPUT_ENABLE;  // drive MCLK for the codec

  // WM8994 standard frame: 128-bit frame, 64-bit active, FS = channel id.
  g_sai.FrameInit.FrameLength = 128;
  g_sai.FrameInit.ActiveFrameLength = 64;
  g_sai.FrameInit.FSDefinition = SAI_FS_CHANNEL_IDENTIFICATION;
  g_sai.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  g_sai.FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;

  // Four 16-bit slots; the codec listens on slots 0 (left) and 2 (right).
  g_sai.SlotInit.FirstBitOffset = 0;
  g_sai.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  g_sai.SlotInit.SlotNumber = 4;
  g_sai.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_2;

  if (HAL_SAI_Init(&g_sai) != HAL_OK) {
    return false;
  }
  // Enabling the block starts MCLK toggling so the codec can lock its PLL.
  __HAL_SAI_ENABLE(&g_sai);
  return true;
}

void start(RenderFn render) {
  g_render = render;
  // Prime the whole buffer so the first samples out are real audio.
  if (g_render != nullptr) {
    g_render(&g_buffer[0], kTotalSamples / kChannels);
  }
  HAL_SAI_Transmit_DMA(&g_sai, reinterpret_cast<uint8_t*>(g_buffer),
                       kTotalSamples);
}

void stop() { HAL_SAI_DMAStop(&g_sai); }

}  // namespace audio_out

// --- DMA double-buffer callbacks (run in ISR context) ----------------------

extern "C" void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef* hsai) {
  if (hsai->Instance == SAI2_Block_A && g_render != nullptr) {
    g_render(&g_buffer[0], kFramesPerBuffer);  // refill first half
  }
}

extern "C" void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef* hsai) {
  if (hsai->Instance == SAI2_Block_A && g_render != nullptr) {
    g_render(&g_buffer[kHalfSamples], kFramesPerBuffer);  // refill second half
  }
}

extern "C" void DMA2_Stream1_IRQHandler() { HAL_DMA_IRQHandler(&g_dma); }
