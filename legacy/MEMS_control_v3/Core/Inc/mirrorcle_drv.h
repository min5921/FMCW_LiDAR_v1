#ifndef MIRRORCLE_DRV_H
#define MIRRORCLE_DRV_H

#include <stdint.h>

void Mirrorcle_InitSequence(void);
void Mirrorcle_LoadAllBias(uint16_t bias_code);
void Mirrorcle_EnableOutput(void);
void Mirrorcle_DisableOutput(void);
void Mirrorcle_TestDAC_A(uint16_t code);
void Mirrorcle_TestAllChannels(uint16_t a, uint16_t b, uint16_t c, uint16_t d);
void Mirrorcle_FullInit(uint16_t bias_code);
void Mirrorcle_PrepareForStart(uint16_t bias_code);



#endif
