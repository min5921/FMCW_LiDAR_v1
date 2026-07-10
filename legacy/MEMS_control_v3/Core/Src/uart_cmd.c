#include "uart_cmd.h"
#include "frame_player.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static UART_HandleTypeDef *g_huart = NULL;
static uint8_t g_rx_byte = 0;

#define CMD_BUF_LEN 128
static char g_cmd_buf[CMD_BUF_LEN];
static uint32_t g_cmd_idx = 0;
static uint8_t g_cmd_overflow = 0;


//-----------------------------------------------------------------------------------------------

void UARTCMD_SendAckCount(const char *prefix, uint32_t count)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s,%lu\r\n", prefix, (unsigned long)count);
    HAL_UART_Transmit(g_huart, (uint8_t *)buf, strlen(buf), 100);
}

void UARTCMD_SendAck(const char *msg)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\r\n", msg);
    HAL_UART_Transmit(g_huart, (uint8_t *)buf, strlen(buf), 100);
}

void UARTCMD_SendString(const char *s)
{
    if (g_huart == NULL || s == NULL)
        return;

    HAL_UART_Transmit(g_huart, (uint8_t *)s, strlen(s), 100);
}

static void UARTCMD_ProcessLine(const char *line)
{
    if (line == NULL || line[0] == '\0')
        return;

    if (strcmp(line, "CLR") == 0)
    {
        FramePlayer_ResetBuffer();
        UARTCMD_SendAck("ACK:CLR");
        return;
    }

    if (strcmp(line, "LOAD_DONE") == 0)
    {
        UARTCMD_SendAckCount("ACK:LOAD_DONE", FramePlayer_GetLoadedCount());
        return;
    }

    if (strcmp(line, "START") == 0)
    {
        if (FramePlayer_GetLoadedCount() == 0)
        {
            UARTCMD_SendString("ERR:START_NO_DATA\r\n");
            return;
        }

        if (FramePlayer_Start())
            UARTCMD_SendAck("ACK:START");
        else
            UARTCMD_SendString("ERR:START_FAILED\r\n");

        return;
    }

    if (strcmp(line, "STOP") == 0)
    {
        FramePlayer_RequestStop();
        UARTCMD_SendAck("ACK:STOP");
        return;
    }

    if (strncmp(line, "DATA,", 5) == 0)
    {
        unsigned long a, b, c, d, m;
        int n;

        n = sscanf(line + 5, "%lu,%lu,%lu,%lu,%lu", &a, &b, &c, &d, &m);

        if (n == 5)
        {
            if (!FramePlayer_LoadFrameABCD(
                    (uint16_t)a,
                    (uint16_t)b,
                    (uint16_t)c,
                    (uint16_t)d,
                    (uint8_t)m))
            {
                UARTCMD_SendString("ERR:BUFFER_FULL\r\n");
            }
        }
        else
        {
            UARTCMD_SendString("ERR:BAD_DATA:");
            UARTCMD_SendString(line);
            UARTCMD_SendString("\r\n");
        }

        return;
    }

    UARTCMD_SendString("ERR:UNKNOWN_CMD:");
    UARTCMD_SendString(line);
    UARTCMD_SendString("\r\n");
}


//-----------------------------------------------------------------------------------------------
void UARTCMD_Init(UART_HandleTypeDef *huart)
{
    g_huart = huart;
    g_cmd_idx = 0;
    g_cmd_overflow = 0;
    memset(g_cmd_buf, 0, sizeof(g_cmd_buf));
}

void UARTCMD_StartReceiveIT(void)
{
    if (g_huart == NULL)
        return;

    HAL_UART_Receive_IT(g_huart, &g_rx_byte, 1);
}

void UARTCMD_RxByteCallback(uint8_t byte)
{
    if (byte == '\r')
        return;

    if (byte == '\n')
    {
        if (g_cmd_overflow)
        {
            g_cmd_idx = 0;
            g_cmd_overflow = 0;
            UARTCMD_SendString("ERR:CMD_TOO_LONG\r\n");
            return;
        }

        g_cmd_buf[g_cmd_idx] = '\0';
        UARTCMD_ProcessLine(g_cmd_buf);
        g_cmd_idx = 0;
        return;
    }

    if (g_cmd_idx < (CMD_BUF_LEN - 1U))
    {
        g_cmd_buf[g_cmd_idx++] = (char)byte;
    }
    else
    {
        g_cmd_overflow = 1;
    }
}
