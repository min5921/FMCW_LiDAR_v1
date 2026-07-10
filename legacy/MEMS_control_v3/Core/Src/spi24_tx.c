

#include "spi24_tx.h"

static SPI_HandleTypeDef *g_hspi = NULL;

void SPI24_Init(SPI_HandleTypeDef *hspi)
{
    g_hspi = hspi;
}

void SPI24_CS_Low(void)
{
    CS_GPIO_Port->BSRR = ((uint32_t)CS_Pin << 16U);
}

void SPI24_CS_High(void)
{
    CS_GPIO_Port->BSRR = CS_Pin;
}

void SPI24_SendWord_Reg(uint32_t data24)
{
    SPI_TypeDef *spi;
    uint32_t tx;

    if (g_hspi == NULL)
        return;

    spi = g_hspi->Instance;
    tx = data24 & 0x00FFFFFFU;

    SPI24_CS_Low();

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;
    MODIFY_REG(spi->CR2, SPI_CR2_TSIZE, (1U << SPI_CR2_TSIZE_Pos));
    SET_BIT(spi->CR1, SPI_CR1_SPE);
    SET_BIT(spi->CR1, SPI_CR1_CSTART);

    while ((spi->SR & SPI_SR_TXP) == 0U)
    {
    }

    *(__IO uint32_t *)&spi->TXDR = tx;

    while ((spi->SR & SPI_SR_EOT) == 0U)
    {
    }

    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC;

    SPI24_CS_High();
}

void SPI24_SendArray_Reg(const uint32_t *arr, uint32_t count)
{
    uint32_t i;

    if (arr == NULL || count == 0U)
        return;

    for (i = 0; i < count; i++)
    {
        SPI24_SendWord_Reg(arr[i]);
    }
}
