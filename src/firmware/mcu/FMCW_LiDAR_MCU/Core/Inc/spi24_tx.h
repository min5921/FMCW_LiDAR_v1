#ifndef SPI24_TX_H
#define SPI24_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef enum
{
    SPI24_STATUS_OK = 0,
    SPI24_STATUS_NOT_INITIALIZED,
    SPI24_STATUS_INVALID_ARGUMENT,
    SPI24_STATUS_TX_READY_TIMEOUT,
    SPI24_STATUS_END_TIMEOUT
} SPI24_Status;

void SPI24_Init(SPI_HandleTypeDef *hspi);

void SPI24_CS_High(void);
void SPI24_CS_Low(void);

uint8_t SPI24_SendWord_Reg(uint32_t data24);
uint8_t SPI24_SendArray_Reg(const uint32_t *arr, uint32_t count);
SPI24_Status SPI24_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif
