#ifndef FRAME_PLAYER_H
#define FRAME_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define FRAME_BUF_LEN                  15000U
#define FRAME_MARKER_HIGH_THRESHOLD    200U

typedef struct
{
    uint32_t word[4];
    uint8_t marker;
} FrameData;

typedef enum
{
    PLAYER_IDLE = 0,
    PLAYER_READY,
    PLAYER_PLAYING
} PlayerState;

void FramePlayer_Init(TIM_HandleTypeDef *htim);
void FramePlayer_ResetBuffer(void);

uint8_t FramePlayer_LoadFrame(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint8_t m);
uint8_t FramePlayer_LoadFrameABCD(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint8_t m);

uint8_t FramePlayer_Start(void);
void FramePlayer_RequestStop(void);
void FramePlayer_StopImmediate(void);
void FramePlayer_OnTimTick(void);

PlayerState FramePlayer_GetState(void);
uint32_t FramePlayer_GetLoadedCount(void);
uint32_t FramePlayer_GetPlayIndex(void);

#ifdef __cplusplus
}
#endif

#endif
