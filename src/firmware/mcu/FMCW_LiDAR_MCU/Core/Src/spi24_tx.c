

#include "spi24_tx.h"

#define SPI24_POLL_LIMIT 100000U

static SPI_HandleTypeDef *g_hspi = NULL;
static volatile SPI24_Status g_status = SPI24_STATUS_NOT_INITIALIZED;

static uint8_t SPI24_WaitForFlag(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t remaining = SPI24_POLL_LIMIT;

    while (remaining > 0U)
    {
        if ((*reg & mask) != 0U)
        {
            return 1U;
        }
        remaining--;
    }

    return 0U;
}

static void SPI24_AbortTransfer(SPI_TypeDef *spi, SPI24_Status status)
{
    CLEAR_BIT(spi->CR1, SPI_CR1_SPE);
    SPI24_CS_High();
    g_status = status;
}

void SPI24_Init(SPI_HandleTypeDef *hspi)
{
    g_hspi = hspi;
    g_status = (hspi != NULL) ? SPI24_STATUS_OK : SPI24_STATUS_NOT_INITIALIZED;
}

void SPI24_CS_Low(void)
{
    CS_GPIO_Port->BSRR = ((uint32_t)CS_Pin << 16U);
}

void SPI24_CS_High(void)
{
    CS_GPIO_Port->BSRR = CS_Pin;
}

uint8_t SPI24_SendWord_Reg(uint32_t data24)
{
    SPI_TypeDef *spi;
    uint32_t tx;

    if (g_hspi == NULL)
    {
        g_status = SPI24_STATUS_NOT_INITIALIZED;
        return 0U;
    }

    spi = g_hspi->Instance;
    tx = data24 & 0x00FFFFFFU;
    g_status = SPI24_STATUS_OK;

    SPI24_CS_Low();

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    MODIFY_REG(spi->CR2, SPI_CR2_TSIZE, (1U << SPI_CR2_TSIZE_Pos));
    SET_BIT(spi->CR1, SPI_CR1_SPE);
    SET_BIT(spi->CR1, SPI_CR1_CSTART);

    if (!SPI24_WaitForFlag(&spi->SR, SPI_SR_TXP))
    {
        SPI24_AbortTransfer(spi, SPI24_STATUS_TX_READY_TIMEOUT);
        return 0U;
    }

    *(__IO uint32_t *)&spi->TXDR = tx;

    if (!SPI24_WaitForFlag(&spi->SR, SPI_SR_EOT))
    {
        SPI24_AbortTransfer(spi, SPI24_STATUS_END_TIMEOUT);
        return 0U;
    }

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;

    SPI24_CS_High();
    return 1U;
}

uint8_t SPI24_SendArray_Reg(const uint32_t *arr, uint32_t count)
{
    uint32_t i;

    if (arr == NULL || count == 0U)
    {
        g_status = SPI24_STATUS_INVALID_ARGUMENT;
        return 0U;
    }

    for (i = 0; i < count; i++)
    {
        if (!SPI24_SendWord_Reg(arr[i]))
        {
            return 0U;
        }
    }

    return 1U;
}

SPI24_Status SPI24_GetStatus(void)
{
    return g_status;
}
