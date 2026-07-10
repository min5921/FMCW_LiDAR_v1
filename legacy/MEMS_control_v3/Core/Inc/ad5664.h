#ifndef AD5664_H
#define AD5664_H

#include <stdint.h>

#define AD5664_CMD_WRITE_INPUT_N              0x0U
#define AD5664_CMD_UPDATE_DAC_N               0x1U
#define AD5664_CMD_WRITE_INPUT_N_UPDATE_ALL   0x2U
#define AD5664_CMD_WRITE_UPDATE_N             0x3U
#define AD5664_CMD_POWERDOWN                  0x4U
#define AD5664_CMD_RESET                      0x5U
#define AD5664_CMD_LDAC_SETUP                 0x6U
#define AD5664_CMD_INTERNAL_REF_SETUP         0x7U

#define AD5664_ADDR_DAC_A                     0x0U
#define AD5664_ADDR_DAC_B                     0x1U
#define AD5664_ADDR_DAC_C                     0x2U
#define AD5664_ADDR_DAC_D                     0x3U
#define AD5664_ADDR_ALL_DACS                  0x7U

uint32_t AD5664_MakeWord(uint8_t cmd, uint8_t addr, uint16_t data);
void AD5664_MakeABCDWords_UpdateOnD(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint32_t out_words[4]);

#endif
