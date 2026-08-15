#include "comm.h"
#include "NekoDriverIO.h"
#include "io.h"
#include "mem.h"
#include "nand.h"
#include "nor.h"
#include "ram.h"
#include "state.h"

#include <stdint.h>
#include <string.h>

extern nc2k_states_t nc2k_states;
extern unsigned keypadmatrix[8][8];

bool console_on = false;
string console_input;

uint8_t *rom_buff = 0;
uint8_t *rom_volume0[0x100];
uint8_t *rom_volume1[0x100];
uint8_t *rom_volume2[0x100];

void init_rom() {}
void LoadRom() {}
void rom_switcher() {}

void clear_cmds() {}

void IO_API Write0F(uint8_t addr, uint8_t value)
{
    nc2k_states.ram_io[addr] = value;
    super_switch();
    rw0f_b4_DIR00 = (value & 0x10u) != 0;
    rw0f_b5_DIR01 = (value & 0x20u) != 0;
    rw0f_b6_DIR023 = (value & 0x40u) != 0;
    rw0f_b7_DIR047 = (value & 0x80u) != 0;
}

bool dummy_io_for_read(uint16_t addr, uint8_t &value)
{
    (void)addr;
    (void)value;
    return false;
}

bool dummy_io_for_write(uint16_t addr, uint8_t value)
{
    (void)addr;
    (void)value;
    return false;
}

bool is_nc2600_rom() { return nand_magic[8] == '1'; }
bool is_nc2000_rom() { return nand_magic[8] == '0' && nor_buff[2] == 0x36; }
bool is_nc2010_rom() { return nand_magic[8] == '0' && nor_buff[2] == 0x4f; }

void set_warm_reset_flag()
{
    nc2k_states.ram_io[2] = 1;
    nc2k_states.ram_io[3] = 1;
}

void bbk_set_matrix_key(int row, int column, bool pressed)
{
    if (row < 0 || row >= 8 || column < 0 || column >= 8) return;
    if (pressed && column < 2) {
        void warm_reset_if_clkoff();
        warm_reset_if_clkoff();
    }
    keypadmatrix[row][column] = pressed ? 1u : 0u;
}

void bbk_release_all_keys()
{
    memset(keypadmatrix, 0, sizeof(keypadmatrix));
}
