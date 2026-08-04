#ifndef UART_CMD_H
#define UART_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

void UARTCMD_Init(UART_HandleTypeDef *huart);
uint8_t UARTCMD_StartReceiveIT(void);
void UARTCMD_RxCpltCallback(UART_HandleTypeDef *huart);
void UARTCMD_ErrorCallback(UART_HandleTypeDef *huart);
void UARTCMD_Process(void);

void UARTCMD_SendString(const char *text);
void UARTCMD_SendAck(const char *message);
void UARTCMD_SendAckCount(const char *prefix, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
