#include "sweep_io.h"

extern ADC_HandleTypeDef hadc1;
extern DAC_HandleTypeDef hdac1;

#define ADC_CHANNEL_COUNT 3U
#define ADC_SCAN_RATE_HZ 1000U
#define DMA_TIMEOUT_MS 100U

typedef enum
{
    TRANSFER_PENDING,
    TRANSFER_COMPLETE,
    TRANSFER_FAILED
} TransferState;

static DMA_HandleTypeDef adc_dma;
static DMA_HandleTypeDef dac1_dma;
static DMA_HandleTypeDef dac2_dma;
static volatile TransferState transfer_state;
static volatile uint32_t adc_buffer[ADC_AVERAGE_COUNT][ADC_CHANNEL_COUNT];

static void ConfigureDma(DMA_HandleTypeDef *dma, DMA_Channel_TypeDef *channel,
                         uint32_t request, bool adc)
{
    dma->Instance = channel;
    dma->Init.Request = request;
    dma->Init.Direction = adc ? DMA_PERIPH_TO_MEMORY : DMA_MEMORY_TO_PERIPH;
    dma->Init.PeriphInc = DMA_PINC_DISABLE;
    dma->Init.MemInc = adc ? DMA_MINC_ENABLE : DMA_MINC_DISABLE;
    dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    dma->Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    dma->Init.Mode = adc ? DMA_CIRCULAR : DMA_NORMAL;
    dma->Init.Priority = adc ? DMA_PRIORITY_HIGH : DMA_PRIORITY_MEDIUM;
    if (HAL_DMA_Init(dma) != HAL_OK)
        Sweep_Fatal(adc ? 2U : 5U);
}

static void ConfigureAdcScan(void)
{
    const uint32_t channels[] = {ADC_CHANNEL_6, ADC_CHANNEL_5, ADC_CHANNEL_8};
    const uint32_t ranks[] = {ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3};
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_1 | GPIO_PIN_3;
    pins.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    pins.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &pins);

    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.NbrOfConversion = ADC_CHANNEL_COUNT;
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK)
        Sweep_Fatal(2U);

    ADC_ChannelConfTypeDef config = {0};
    config.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
    config.SingleDiff = ADC_SINGLE_ENDED;
    config.OffsetNumber = ADC_OFFSET_NONE;
    for (uint32_t i = 0U; i < ADC_CHANNEL_COUNT; ++i)
    {
        config.Channel = channels[i];
        config.Rank = ranks[i];
        if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK)
            Sweep_Fatal(2U);
    }
}

static void ConfigureDacTrigger(void)
{
    DAC_ChannelConfTypeDef config = {0};
    config.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    config.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
    config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    config.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    config.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    if (HAL_DAC_ConfigChannel(&hdac1, &config, DAC_CHANNEL_1) != HAL_OK ||
        HAL_DAC_ConfigChannel(&hdac1, &config, DAC_CHANNEL_2) != HAL_OK)
        Sweep_Fatal(5U);
}

static void ConfigureTimer(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) >= RCC_CFGR_PPRE1_DIV2)
        timer_clock *= 2U;
    const uint32_t ticks = timer_clock / ADC_SCAN_RATE_HZ;
    if (ticks == 0U || ticks > UINT16_MAX + 1U)
        Sweep_Fatal(2U);

    TIM6->CR1 = 0U;
    TIM6->PSC = 0U;
    TIM6->ARR = ticks - 1U;
    TIM6->CR2 = TIM_CR2_MMS_1; /* Update event is TRGO. */
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0U;
}

static bool WaitForTransfer(void)
{
    const uint32_t started = HAL_GetTick();
    while (transfer_state == TRANSFER_PENDING && HAL_GetTick() - started < DMA_TIMEOUT_MS)
        __WFI(); /* DMA interrupts and SysTick wake the core. */
    return transfer_state == TRANSFER_COMPLETE;
}

void Sweep_StartHardware(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    /* STM32L432 RM0394 DMA1 request mapping. */
    ConfigureDma(&adc_dma, DMA1_Channel1, DMA_REQUEST_0, true);
    ConfigureDma(&dac1_dma, DMA1_Channel3, DMA_REQUEST_6, false);
    ConfigureDma(&dac2_dma, DMA1_Channel4, DMA_REQUEST_5, false);
    __HAL_LINKDMA(&hadc1, DMA_Handle, adc_dma);
    __HAL_LINKDMA(&hdac1, DMA_Handle1, dac1_dma);
    __HAL_LINKDMA(&hdac1, DMA_Handle2, dac2_dma);

    ConfigureAdcScan();
    ConfigureDacTrigger();
    ConfigureTimer();
    const IRQn_Type interrupts[] = {DMA1_Channel1_IRQn, DMA1_Channel3_IRQn,
                                    DMA1_Channel4_IRQn, ADC1_IRQn, TIM6_DAC_IRQn};
    for (uint32_t i = 0U; i < sizeof(interrupts) / sizeof(interrupts[0]); ++i)
    {
        HAL_NVIC_SetPriority(interrupts[i], 1U, 0U);
        HAL_NVIC_EnableIRQ(interrupts[i]);
    }

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
        Sweep_Fatal(2U);
    Sweep_SetOutput(DAC_CHANNEL_1, 0.0f);
    Sweep_SetOutput(DAC_CHANNEL_2, 0.0f);
    HAL_Delay(100U);
}

void Sweep_SetOutput(uint32_t channel, float volts)
{
    if (volts < 0.0f) volts = 0.0f;
    if (volts > ADC_VREF_ASSUMED_V) volts = ADC_VREF_ASSUMED_V;
    uint32_t code = (uint32_t)(volts / ADC_VREF_ASSUMED_V * 4095.0f + 0.5f);
    const uint32_t request = channel == DAC_CHANNEL_1 ? DAC_CR_DMAEN1 : DAC_CR_DMAEN2;

    transfer_state = TRANSFER_PENDING;
    if (HAL_DAC_Start_DMA(&hdac1, channel, &code, 1U, DAC_ALIGN_12B_R) != HAL_OK)
        Sweep_Fatal(5U);

    /* First update requests the new code. A second update latches it into DOR. */
    TIM6->EGR = TIM_EGR_UG;
    const bool loaded = WaitForTransfer();
    CLEAR_BIT(hdac1.Instance->CR, request);
    if (!loaded)
    {
        (void)HAL_DAC_Stop_DMA(&hdac1, channel);
        Sweep_Fatal(5U);
    }

    TIM6->EGR = TIM_EGR_UG;
    const uint32_t started = HAL_GetTick();
    while (HAL_DAC_GetValue(&hdac1, channel) != code)
    {
        if (HAL_GetTick() - started >= DMA_TIMEOUT_MS)
            Sweep_Fatal(5U);
    }
}

SweepSample Sweep_ReadSample(void)
{
    transfer_state = TRANSFER_PENDING;
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&adc_buffer[0][0],
                          ADC_AVERAGE_COUNT * ADC_CHANNEL_COUNT) != HAL_OK)
        Sweep_Fatal(2U);

    TIM6->CNT = 0U;
    SET_BIT(TIM6->CR1, TIM_CR1_CEN);
    const bool captured = WaitForTransfer();
    CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
    if (HAL_ADC_Stop_DMA(&hadc1) != HAL_OK || !captured || HAL_ADC_GetError(&hadc1) != HAL_ADC_ERROR_NONE)
        Sweep_Fatal(2U);

    uint32_t totals[ADC_CHANNEL_COUNT] = {0};
    for (uint32_t scan = 0U; scan < ADC_AVERAGE_COUNT; ++scan)
        for (uint32_t rank = 0U; rank < ADC_CHANNEL_COUNT; ++rank)
            totals[rank] += adc_buffer[scan][rank];

    const float scale = ADC_VREF_ASSUMED_V / (4095.0f * ADC_AVERAGE_COUNT);
    return (SweepSample){.supply_v = totals[0] * scale, .drain_v = totals[1] * scale,
                         .gate_v = totals[2] * scale};
}

void DMA1_Channel1_IRQHandler(void) { HAL_DMA_IRQHandler(&adc_dma); }
void DMA1_Channel3_IRQHandler(void) { HAL_DMA_IRQHandler(&dac1_dma); }
void DMA1_Channel4_IRQHandler(void) { HAL_DMA_IRQHandler(&dac2_dma); }
void ADC1_IRQHandler(void) { HAL_ADC_IRQHandler(&hadc1); }
void TIM6_DAC_IRQHandler(void) { HAL_DAC_IRQHandler(&hdac1); }

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &hadc1)
    {
        CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
        transfer_state = TRANSFER_COMPLETE;
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
    if (adc == &hadc1)
    {
        CLEAR_BIT(TIM6->CR1, TIM_CR1_CEN);
        transfer_state = TRANSFER_FAILED;
    }
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *dac)
{
    if (dac == &hdac1) transfer_state = TRANSFER_COMPLETE;
}

void HAL_DACEx_ConvCpltCallbackCh2(DAC_HandleTypeDef *dac)
{
    if (dac == &hdac1) transfer_state = TRANSFER_COMPLETE;
}

void HAL_DAC_ErrorCallbackCh1(DAC_HandleTypeDef *dac)
{
    if (dac == &hdac1) transfer_state = TRANSFER_FAILED;
}

void HAL_DACEx_ErrorCallbackCh2(DAC_HandleTypeDef *dac)
{
    if (dac == &hdac1) transfer_state = TRANSFER_FAILED;
}

void HAL_DAC_DMAUnderrunCallbackCh1(DAC_HandleTypeDef *dac)
{
    HAL_DAC_ErrorCallbackCh1(dac);
}

void HAL_DACEx_DMAUnderrunCallbackCh2(DAC_HandleTypeDef *dac)
{
    HAL_DACEx_ErrorCallbackCh2(dac);
}
