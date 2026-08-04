#ifndef MIRRORCLE_DRV_H
#define MIRRORCLE_DRV_H

#include <stdint.h>

#define MIRRORCLE_DEFAULT_BIAS_CODE 29491U

uint8_t Mirrorcle_InitSequence(void);
uint8_t Mirrorcle_LoadAllBias(uint16_t bias_code);
void Mirrorcle_EnableOutput(void);
void Mirrorcle_DisableOutput(void);
uint8_t Mirrorcle_TestDAC_A(uint16_t code);
uint8_t Mirrorcle_TestAllChannels(uint16_t a, uint16_t b, uint16_t c, uint16_t d);
uint8_t Mirrorcle_FullInit(uint16_t bias_code);
uint8_t Mirrorcle_PrepareForStart(uint16_t bias_code);

#endif
