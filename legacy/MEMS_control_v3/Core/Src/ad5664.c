


#include "ad5664.h"

uint32_t AD5664_MakeWord(uint8_t cmd, uint8_t addr, uint16_t data)
{
    uint32_t word = 0;

    // DB23:DB22 = don't care = 0
    // DB21:DB19 = command (3bit)
    // DB18:DB16 = address (3bit)
    // DB15:DB0  = data (16bit)

    word |= ((uint32_t)(cmd  & 0x07U) << 19);
    word |= ((uint32_t)(addr & 0x07U) << 16);
    word |= (uint32_t)data;

    return word & 0x00FFFFFFU;
}

void AD5664_MakeABCDWords_UpdateOnD(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t out_words[4])
{
    if (out_words == 0)
        return;

    // A, B, C는 input register에만 씀
    out_words[0] = AD5664_MakeWord(AD5664_CMD_WRITE_INPUT_N, AD5664_ADDR_DAC_A, a);
    out_words[1] = AD5664_MakeWord(AD5664_CMD_WRITE_INPUT_N, AD5664_ADDR_DAC_B, b);
    out_words[2] = AD5664_MakeWord(AD5664_CMD_WRITE_INPUT_N, AD5664_ADDR_DAC_C, c);

    // D를 보낼 때 전체 업데이트
    out_words[3] = AD5664_MakeWord(AD5664_CMD_WRITE_INPUT_N_UPDATE_ALL, AD5664_ADDR_DAC_D, d);
}
