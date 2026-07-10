#ifndef SPI24_TX_H
#define SPI24_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

void SPI24_Init(SPI_HandleTypeDef *hspi);

void SPI24_CS_High(void);
void SPI24_CS_Low(void);

void SPI24_SendWord_Reg(uint32_t data24);
void SPI24_SendArray_Reg(const uint32_t *arr, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
