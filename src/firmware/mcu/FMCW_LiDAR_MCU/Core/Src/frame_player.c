#include "frame_player.h"
#include "spi24_tx.h"
#include "ad5664.h"
#include "mirrorcle_drv.h"

static TIM_HandleTypeDef *g_htim = NULL;
static FrameData g_frame_buf[FRAME_BUF_LEN];
static volatile uint32_t g_loaded_count = 0U;
static volatile uint32_t g_play_idx = 0U;
static volatile uint8_t g_stop_req = 0U;
static volatile PlayerState g_state = PLAYER_IDLE;

static inline void M_High(void)
{
    M_Btrig_GPIO_Port->BSRR = M_Btrig_Pin;
}

static inline void M_Low(void)
{
    M_Btrig_GPIO_Port->BSRR = ((uint32_t)M_Btrig_Pin << 16U);
}

static uint8_t MarkerIsHigh(uint8_t m)
{
    return (m >= FRAME_MARKER_HIGH_THRESHOLD) ? 1U : 0U;
}

void FramePlayer_Init(TIM_HandleTypeDef *htim)
{
    g_htim = htim;
    g_loaded_count = 0U;
    g_play_idx = 0U;
    g_stop_req = 0U;
    g_state = PLAYER_IDLE;
    M_Low();
    Mirrorcle_DisableOutput();
}

void FramePlayer_ResetBuffer(void)
{
    uint32_t primask;

    if (g_htim != NULL)
    {
        HAL_TIM_Base_Stop_IT(g_htim);
    }

    Mirrorcle_DisableOutput();
    M_Low();

    primask = __get_PRIMASK();
    __disable_irq();
    g_loaded_count = 0U;
    g_play_idx = 0U;
    g_stop_req = 0U;
    g_state = PLAYER_IDLE;
    if (primask == 0U)
    {
        __enable_irq();
    }

    (void)Mirrorcle_LoadAllBias(MIRRORCLE_DEFAULT_BIAS_CODE);
}

uint8_t FramePlayer_LoadFrameABCD(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t m)
{
    uint32_t words[4];
    const uint32_t index = g_loaded_count;

    if (g_state == PLAYER_PLAYING || index >= FRAME_BUF_LEN)
    {
        return 0U;
    }

    AD5664_MakeABCDWords_UpdateOnD(a, b, c, d, words);
    g_frame_buf[index].word[0] = words[0];
    g_frame_buf[index].word[1] = words[1];
    g_frame_buf[index].word[2] = words[2];
    g_frame_buf[index].word[3] = words[3];
    g_frame_buf[index].marker = MarkerIsHigh(m);

    g_loaded_count = index + 1U;
    g_state = PLAYER_READY;
    return 1U;
}

uint8_t FramePlayer_LoadFrame(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint8_t m)
{
    const uint32_t index = g_loaded_count;

    if (g_state == PLAYER_PLAYING || index >= FRAME_BUF_LEN)
    {
        return 0U;
    }

    g_frame_buf[index].word[0] = w0 & 0x00FFFFFFU;
    g_frame_buf[index].word[1] = w1 & 0x00FFFFFFU;
    g_frame_buf[index].word[2] = w2 & 0x00FFFFFFU;
    g_frame_buf[index].word[3] = w3 & 0x00FFFFFFU;
    g_frame_buf[index].marker = MarkerIsHigh(m);

    g_loaded_count = index + 1U;
    g_state = PLAYER_READY;
    return 1U;
}

uint8_t FramePlayer_Start(void)
{
    if (g_htim == NULL || g_loaded_count == 0U || g_state == PLAYER_PLAYING)
    {
        return 0U;
    }

    HAL_TIM_Base_Stop_IT(g_htim);
    if (!Mirrorcle_PrepareForStart(MIRRORCLE_DEFAULT_BIAS_CODE))
    {
        return 0U;
    }
    M_Low();

    g_play_idx = 0U;
    g_stop_req = 0U;
    __HAL_TIM_SET_COUNTER(g_htim, 0U);
    __HAL_TIM_CLEAR_FLAG(g_htim, TIM_FLAG_UPDATE);

    g_state = PLAYER_PLAYING;
    Mirrorcle_EnableOutput();
    if (HAL_TIM_Base_Start_IT(g_htim) != HAL_OK)
    {
        Mirrorcle_DisableOutput();
        M_Low();
        g_state = PLAYER_READY;
        return 0U;
    }

    return 1U;
}

void FramePlayer_RequestStop(void)
{
    g_stop_req = 1U;
}

void FramePlayer_StopImmediate(void)
{
    if (g_htim != NULL)
    {
        HAL_TIM_Base_Stop_IT(g_htim);
    }

    Mirrorcle_DisableOutput();
    M_Low();
    g_stop_req = 0U;
    g_play_idx = 0U;
    (void)Mirrorcle_LoadAllBias(MIRRORCLE_DEFAULT_BIAS_CODE);
    g_state = (g_loaded_count > 0U) ? PLAYER_READY : PLAYER_IDLE;
}

void FramePlayer_OnTimTick(void)
{
    const FrameData *frame;

    if (g_state != PLAYER_PLAYING)
    {
        return;
    }

    if (g_stop_req != 0U || g_loaded_count == 0U)
    {
        FramePlayer_StopImmediate();
        return;
    }

    frame = &g_frame_buf[g_play_idx];
    if (frame->marker != 0U)
    {
        M_High();
    }
    else
    {
        M_Low();
    }

    if (!SPI24_SendArray_Reg(frame->word, 4U))
    {
        HAL_TIM_Base_Stop_IT(g_htim);
        Mirrorcle_DisableOutput();
        M_Low();
        g_play_idx = 0U;
        g_stop_req = 0U;
        g_state = PLAYER_READY;
        return;
    }

    g_play_idx++;
    if (g_play_idx >= g_loaded_count)
    {
        g_play_idx = 0U;
    }
}

PlayerState FramePlayer_GetState(void)
{
    return g_state;
}

uint32_t FramePlayer_GetLoadedCount(void)
{
    return g_loaded_count;
}

uint32_t FramePlayer_GetPlayIndex(void)
{
    return g_play_idx;
}
