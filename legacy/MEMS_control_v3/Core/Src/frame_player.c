#include "frame_player.h"
#include "spi24_tx.h"
#include "ad5664.h"
#include "mirrorcle_drv.h"




static TIM_HandleTypeDef *g_htim = NULL;

static volatile FrameData g_frame_buf[FRAME_BUF_LEN];
static volatile uint32_t g_loaded_count = 0;
static volatile uint32_t g_play_idx = 0;

static volatile uint8_t g_stop_req = 0;
static volatile PlayerState g_state = PLAYER_IDLE;


static inline void EN_High(void)
{
    EN_GPIO_Port->BSRR = EN_Pin;
}

static inline void EN_Low(void)
{
    EN_GPIO_Port->BSRR = ((uint32_t)EN_Pin << 16U);
}




//-----------------------------------------------------------------------------------------------

uint8_t FramePlayer_LoadFrameABCD(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t m)
{
    uint32_t words[4];

    if (g_state == PLAYER_PLAYING)
        return 0;

    if (g_loaded_count >= FRAME_BUF_LEN)
        return 0;

    AD5664_MakeABCDWords_UpdateOnD(a, b, c, d, words);

    g_frame_buf[g_loaded_count].word[0] = words[0];
    g_frame_buf[g_loaded_count].word[1] = words[1];
    g_frame_buf[g_loaded_count].word[2] = words[2];
    g_frame_buf[g_loaded_count].word[3] = words[3];
    g_frame_buf[g_loaded_count].m = m;

    g_loaded_count++;
    g_state = PLAYER_READY;

    return 1;
}



/* === M pin 제어: 핀 이름은 네 CubeMX 설정에 맞춰야 함 === */
static inline void M_High(void)
{
	M_Btrig_GPIO_Port->BSRR = M_Btrig_Pin;
}

static inline void M_Low(void)
{
	M_Btrig_GPIO_Port->BSRR = ((uint32_t)M_Btrig_Pin << 16U);
}

void FramePlayer_Init(TIM_HandleTypeDef *htim)
{
    g_htim = htim;
    g_loaded_count = 0;
    g_play_idx = 0;
    g_stop_req = 0;
    g_state = PLAYER_IDLE;
    M_Low();
}

void FramePlayer_ResetBuffer(void)
{
    if (g_htim != NULL)
    {
        HAL_TIM_Base_Stop_IT(g_htim);
    }

    __disable_irq();
    g_loaded_count = 0;
    g_play_idx = 0;
    g_state = PLAYER_IDLE;
    g_stop_req = 0;
    __enable_irq();

    M_Low();
    Mirrorcle_LoadAllBias(29491);
    EN_Low();
}

uint8_t FramePlayer_LoadFrame(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint8_t m)
{
    if (g_state == PLAYER_PLAYING)
        return 0;

    if (g_loaded_count >= FRAME_BUF_LEN)
        return 0;

    g_frame_buf[g_loaded_count].word[0] = w0 & 0x00FFFFFFU;
    g_frame_buf[g_loaded_count].word[1] = w1 & 0x00FFFFFFU;
    g_frame_buf[g_loaded_count].word[2] = w2 & 0x00FFFFFFU;
    g_frame_buf[g_loaded_count].word[3] = w3 & 0x00FFFFFFU;
    g_frame_buf[g_loaded_count].m       = (m ? 1U : 0U);

    g_loaded_count++;
    g_state = PLAYER_READY;

    return 1;
}

uint8_t FramePlayer_Start(void)
{
    if (g_htim == NULL)
        return 0;

    if (g_loaded_count == 0U)
        return 0;

    HAL_TIM_Base_Stop_IT(g_htim);   // 혹시 이전 상태 남았으면 정리

    Mirrorcle_PrepareForStart(29491);

    g_play_idx = 0;
    g_stop_req = 0;

    if (HAL_TIM_Base_Start_IT(g_htim) != HAL_OK)
    {
        EN_Low();
        g_state = PLAYER_READY;
        return 0;
    }

    g_state = PLAYER_PLAYING;
    EN_High();
    return 1;
}

void FramePlayer_RequestStop(void)
{
    g_stop_req = 1;
}

void FramePlayer_StopImmediate(void)
{
    if (g_htim != NULL)
        HAL_TIM_Base_Stop_IT(g_htim);

    g_stop_req = 0;
    g_play_idx = 0;

    M_Low();
    Mirrorcle_LoadAllBias(29491);
    EN_Low();

    if (g_loaded_count > 0U)
        g_state = PLAYER_READY;
    else
        g_state = PLAYER_IDLE;
}

void FramePlayer_OnTimTick(void)
{
    const FrameData *frm;

    if (g_state != PLAYER_PLAYING)
        return;

    if (g_stop_req)
    {
        FramePlayer_StopImmediate();
        return;
    }

    if (g_loaded_count == 0U)
    {
        FramePlayer_StopImmediate();
        return;
    }

    frm = (const FrameData *)&g_frame_buf[g_play_idx];

    if (frm->m >= 200)
        M_High();
    else
        M_Low();

    SPI24_SendArray_Reg(frm->word, 4);



    g_play_idx++;
    if (g_play_idx >= g_loaded_count)
    {
        g_play_idx = 0;
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
