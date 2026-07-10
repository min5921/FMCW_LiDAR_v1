#include "mirrorcle_drv.h"
#include "spi24_tx.h"
#include "ad5664.h"
#include "main.h"
#include "stm32h7xx_hal.h"

/*
 * Mirrorcle ���� ���� �ʱ�ȭ ������:
 * 0x280001 : full reset
 * 0x380001 : enable internal reference
 * 0x20000F : enable all DAC channels
 * 0x300000 : enable software LDAC
 */


void Mirrorcle_FullInit(uint16_t bias_code)
{
    Mirrorcle_DisableOutput();

    SPI24_SendWord_Reg(0x280001U);   // FULL RESET

    SPI24_SendWord_Reg(0x380001U);   // INTERNAL REF ON

    SPI24_SendWord_Reg(0x20000FU);   // ALL DAC ENABLE

    SPI24_SendWord_Reg(0x300000U);   // SOFTWARE LDAC ENABLE

    Mirrorcle_LoadAllBias(bias_code);
}


void Mirrorcle_PrepareForStart(uint16_t bias_code)
{
    Mirrorcle_DisableOutput();

    SPI24_SendWord_Reg(0x380001U);   // INTERNAL REF ON

    SPI24_SendWord_Reg(0x20000FU);   // ALL DAC ENABLE

    SPI24_SendWord_Reg(0x300000U);   // SOFTWARE LDAC ENABLE

    Mirrorcle_LoadAllBias(bias_code);
}



static inline void EN_High(void)
{
	Enable_GPIO_Port->BSRR = Enable_Pin;
}

static inline void EN_Low(void)
{
	Enable_GPIO_Port->BSRR = ((uint32_t)Enable_Pin << 16U);
}

void Mirrorcle_InitSequence(void)
{
    EN_Low();

    SPI24_SendWord_Reg(0x280001U);   // FULL RESET

    SPI24_SendWord_Reg(0x380001U);   // ENABLE INTERNAL REFERENCE

    SPI24_SendWord_Reg(0x20000FU);   // ENABLE ALL DAC CHANNELS

    SPI24_SendWord_Reg(0x300000U);   // ENABLE SOFTWARE LDAC
}

void Mirrorcle_LoadAllBias(uint16_t bias_code)
{
    uint32_t words[4];

    AD5664_MakeABCDWords_UpdateOnD(bias_code, bias_code, bias_code, bias_code, words);

    SPI24_SendWord_Reg(words[0]);
    SPI24_SendWord_Reg(words[1]);
    SPI24_SendWord_Reg(words[2]);
    SPI24_SendWord_Reg(words[3]);
}

void Mirrorcle_EnableOutput(void)
{
    EN_High();
}

void Mirrorcle_DisableOutput(void)
{
    EN_Low();
}

void Mirrorcle_TestDAC_A(uint16_t code)
{
    uint32_t w;

    w = AD5664_MakeWord(AD5664_CMD_WRITE_UPDATE_N,
                        AD5664_ADDR_DAC_A,
                        code);

    SPI24_SendWord_Reg(w);
}

void Mirrorcle_TestAllChannels(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    uint32_t words[4];

    AD5664_MakeABCDWords_UpdateOnD(a, b, c, d, words);

    SPI24_SendWord_Reg(words[0]);
    SPI24_SendWord_Reg(words[1]);
    SPI24_SendWord_Reg(words[2]);
    SPI24_SendWord_Reg(words[3]);
}
