#include "mirrorcle_drv.h"
#include "spi24_tx.h"
#include "ad5664.h"
#include "main.h"
#include "stm32h7xx_hal.h"

#define MIRRORCLE_CMD_FULL_RESET          0x280001U
#define MIRRORCLE_CMD_INTERNAL_REF_ON     0x380001U
#define MIRRORCLE_CMD_ALL_DACS_ON         0x20000FU
#define MIRRORCLE_CMD_SOFTWARE_LDAC_ON    0x300000U

static inline void EN_High(void)
{
    Enable_GPIO_Port->BSRR = Enable_Pin;
}

static inline void EN_Low(void)
{
    Enable_GPIO_Port->BSRR = ((uint32_t)Enable_Pin << 16U);
}

uint8_t Mirrorcle_FullInit(uint16_t bias_code)
{
    Mirrorcle_DisableOutput();
    return SPI24_SendWord_Reg(MIRRORCLE_CMD_FULL_RESET) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_INTERNAL_REF_ON) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_ALL_DACS_ON) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_SOFTWARE_LDAC_ON) &&
           Mirrorcle_LoadAllBias(bias_code);
}

uint8_t Mirrorcle_PrepareForStart(uint16_t bias_code)
{
    Mirrorcle_DisableOutput();
    return SPI24_SendWord_Reg(MIRRORCLE_CMD_INTERNAL_REF_ON) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_ALL_DACS_ON) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_SOFTWARE_LDAC_ON) &&
           Mirrorcle_LoadAllBias(bias_code);
}

uint8_t Mirrorcle_InitSequence(void)
{
    EN_Low();
    return SPI24_SendWord_Reg(MIRRORCLE_CMD_FULL_RESET) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_INTERNAL_REF_ON) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_ALL_DACS_ON) &&
           SPI24_SendWord_Reg(MIRRORCLE_CMD_SOFTWARE_LDAC_ON);
}

uint8_t Mirrorcle_LoadAllBias(uint16_t bias_code)
{
    uint32_t words[4];

    AD5664_MakeABCDWords_UpdateOnD(bias_code, bias_code, bias_code, bias_code, words);
    return SPI24_SendArray_Reg(words, 4U);
}

void Mirrorcle_EnableOutput(void)
{
    EN_High();
}

void Mirrorcle_DisableOutput(void)
{
    EN_Low();
}

uint8_t Mirrorcle_TestDAC_A(uint16_t code)
{
    const uint32_t word = AD5664_MakeWord(AD5664_CMD_WRITE_UPDATE_N,
                                          AD5664_ADDR_DAC_A,
                                          code);

    return SPI24_SendWord_Reg(word);
}

uint8_t Mirrorcle_TestAllChannels(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    uint32_t words[4];

    AD5664_MakeABCDWords_UpdateOnD(a, b, c, d, words);
    return SPI24_SendArray_Reg(words, 4U);
}
