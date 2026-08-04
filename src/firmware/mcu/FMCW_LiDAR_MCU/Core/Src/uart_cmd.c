#include "uart_cmd.h"
#include "frame_player.h"

#include <stdio.h>
#include <string.h>

#define CMD_BUF_LEN       128U
#define RX_RING_LEN       512U
#define RX_RING_MASK      (RX_RING_LEN - 1U)
#define FAST_STOP_LEN     4U

#if (RX_RING_LEN & RX_RING_MASK) != 0
#error RX_RING_LEN must be a power of two
#endif

static UART_HandleTypeDef *g_huart = NULL;
static uint8_t g_rx_byte = 0U;
static uint8_t g_rx_ring[RX_RING_LEN];
static volatile uint16_t g_rx_head = 0U;
static volatile uint16_t g_rx_tail = 0U;
static volatile uint8_t g_rx_overflow = 0U;
static volatile uint8_t g_rx_restart_required = 0U;
static volatile uint8_t g_fast_stop_index = 0U;
static volatile uint8_t g_fast_stop_invalid = 0U;

static char g_cmd_buf[CMD_BUF_LEN];
static uint32_t g_cmd_idx = 0U;
static uint8_t g_cmd_overflow = 0U;

static void UARTCMD_ObserveFastStop(uint8_t byte)
{
    static const uint8_t stop_command[FAST_STOP_LEN] = {'S', 'T', 'O', 'P'};

    if (byte == '\r')
    {
        return;
    }

    if (byte == '\n')
    {
        if (g_fast_stop_invalid == 0U && g_fast_stop_index == FAST_STOP_LEN)
        {
            FramePlayer_RequestStop();
        }
        g_fast_stop_index = 0U;
        g_fast_stop_invalid = 0U;
        return;
    }

    if (g_fast_stop_invalid != 0U)
    {
        return;
    }

    if (g_fast_stop_index < FAST_STOP_LEN && byte == stop_command[g_fast_stop_index])
    {
        g_fast_stop_index++;
    }
    else
    {
        g_fast_stop_invalid = 1U;
    }
}

void UARTCMD_SendString(const char *text)
{
    if (g_huart == NULL || text == NULL)
    {
        return;
    }

    HAL_UART_Transmit(g_huart, (uint8_t *)text, (uint16_t)strlen(text), 100U);
}

void UARTCMD_SendAckCount(const char *prefix, uint32_t count)
{
    char buffer[64];

    if (prefix == NULL)
    {
        return;
    }

    snprintf(buffer, sizeof(buffer), "%s,%lu\r\n", prefix, (unsigned long)count);
    UARTCMD_SendString(buffer);
}

void UARTCMD_SendAck(const char *message)
{
    char buffer[64];

    if (message == NULL)
    {
        return;
    }

    snprintf(buffer, sizeof(buffer), "%s\r\n", message);
    UARTCMD_SendString(buffer);
}

static void UARTCMD_ProcessData(const char *payload)
{
    unsigned long a;
    unsigned long b;
    unsigned long c;
    unsigned long d;
    unsigned long marker;
    int consumed = 0;
    int parsed;

    parsed = sscanf(payload, "%lu,%lu,%lu,%lu,%lu%n",
                    &a, &b, &c, &d, &marker, &consumed);
    if (parsed != 5 || consumed <= 0 || payload[consumed] != '\0')
    {
        UARTCMD_SendString("ERR:BAD_DATA\r\n");
        return;
    }

    if (a > UINT16_MAX || b > UINT16_MAX || c > UINT16_MAX ||
        d > UINT16_MAX || marker > UINT8_MAX)
    {
        UARTCMD_SendString("ERR:DATA_RANGE\r\n");
        return;
    }

    if (!FramePlayer_LoadFrameABCD((uint16_t)a,
                                   (uint16_t)b,
                                   (uint16_t)c,
                                   (uint16_t)d,
                                   (uint8_t)marker))
    {
        UARTCMD_SendString(FramePlayer_GetState() == PLAYER_PLAYING
                               ? "ERR:BUSY\r\n"
                               : "ERR:BUFFER_FULL\r\n");
    }
}

static void UARTCMD_ProcessLine(const char *line)
{
    if (line == NULL || line[0] == '\0')
    {
        return;
    }

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
        if (FramePlayer_GetLoadedCount() == 0U)
        {
            UARTCMD_SendString("ERR:START_NO_DATA\r\n");
        }
        else if (FramePlayer_Start())
        {
            UARTCMD_SendAck("ACK:START");
        }
        else
        {
            UARTCMD_SendString("ERR:START_FAILED\r\n");
        }
        return;
    }

    if (strcmp(line, "STOP") == 0)
    {
        FramePlayer_StopImmediate();
        UARTCMD_SendAckCount("ACK:STOP", FramePlayer_GetLoadedCount());
        return;
    }

    if (strncmp(line, "DATA,", 5U) == 0)
    {
        UARTCMD_ProcessData(line + 5);
        return;
    }

    UARTCMD_SendString("ERR:UNKNOWN_CMD:");
    UARTCMD_SendString(line);
    UARTCMD_SendString("\r\n");
}

static void UARTCMD_ProcessByte(uint8_t byte)
{
    if (byte == '\r')
    {
        return;
    }

    if (byte == '\n')
    {
        if (g_cmd_overflow != 0U)
        {
            g_cmd_idx = 0U;
            g_cmd_overflow = 0U;
            UARTCMD_SendString("ERR:CMD_TOO_LONG\r\n");
            return;
        }

        g_cmd_buf[g_cmd_idx] = '\0';
        UARTCMD_ProcessLine(g_cmd_buf);
        g_cmd_idx = 0U;
        return;
    }

    if (g_cmd_idx < (CMD_BUF_LEN - 1U))
    {
        g_cmd_buf[g_cmd_idx++] = (char)byte;
    }
    else
    {
        g_cmd_overflow = 1U;
    }
}

void UARTCMD_Init(UART_HandleTypeDef *huart)
{
    g_huart = huart;
    g_rx_byte = 0U;
    g_rx_head = 0U;
    g_rx_tail = 0U;
    g_rx_overflow = 0U;
    g_rx_restart_required = 0U;
    g_fast_stop_index = 0U;
    g_fast_stop_invalid = 0U;
    g_cmd_idx = 0U;
    g_cmd_overflow = 0U;
    memset(g_rx_ring, 0, sizeof(g_rx_ring));
    memset(g_cmd_buf, 0, sizeof(g_cmd_buf));
}

uint8_t UARTCMD_StartReceiveIT(void)
{
    if (g_huart == NULL)
    {
        return 0U;
    }

    if (HAL_UART_Receive_IT(g_huart, &g_rx_byte, 1U) != HAL_OK)
    {
        g_rx_restart_required = 1U;
        return 0U;
    }

    g_rx_restart_required = 0U;
    return 1U;
}

void UARTCMD_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next;

    if (huart != g_huart)
    {
        return;
    }

    UARTCMD_ObserveFastStop(g_rx_byte);

    next = (uint16_t)((g_rx_head + 1U) & RX_RING_MASK);
    if (next == g_rx_tail)
    {
        g_rx_overflow = 1U;
    }
    else
    {
        g_rx_ring[g_rx_head] = g_rx_byte;
        __DMB();
        g_rx_head = next;
    }

    if (HAL_UART_Receive_IT(g_huart, &g_rx_byte, 1U) != HAL_OK)
    {
        g_rx_restart_required = 1U;
    }
}

void UARTCMD_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == g_huart)
    {
        g_rx_restart_required = 1U;
    }
}

void UARTCMD_Process(void)
{
    if (g_rx_restart_required != 0U && g_huart != NULL)
    {
        HAL_UART_AbortReceive(g_huart);
        __HAL_UART_CLEAR_OREFLAG(g_huart);
        if (HAL_UART_Receive_IT(g_huart, &g_rx_byte, 1U) == HAL_OK)
        {
            g_rx_restart_required = 0U;
        }
    }

    if (g_rx_overflow != 0U)
    {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        g_rx_tail = g_rx_head;
        g_rx_overflow = 0U;
        if (primask == 0U)
        {
            __enable_irq();
        }

        g_cmd_idx = 0U;
        g_cmd_overflow = 0U;
        UARTCMD_SendString("ERR:RX_OVERFLOW\r\n");
    }

    while (g_rx_tail != g_rx_head)
    {
        const uint8_t byte = g_rx_ring[g_rx_tail];

        g_rx_tail = (uint16_t)((g_rx_tail + 1U) & RX_RING_MASK);
        UARTCMD_ProcessByte(byte);
    }
}
