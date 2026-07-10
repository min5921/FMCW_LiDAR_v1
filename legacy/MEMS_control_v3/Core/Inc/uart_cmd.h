#ifndef UART_CMD_H
#define UART_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

void UARTCMD_Init(UART_HandleTypeDef *huart);
void UARTCMD_StartReceiveIT(void);

void UARTCMD_RxByteCallback(uint8_t byte);

void UARTCMD_SendString(const char *s);
void UARTCMD_SendAck(const char *msg);
void UARTCMD_SendAckCount(const char *prefix, uint32_t count);



#ifdef __cplusplus
}
#endif

#endif
