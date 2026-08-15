#pragma once
#include "comm.h"

extern uint8_t* nor_banks[0x20];
#ifdef BBK9588
extern uint8_t nor_buff[512*1024];
#else
extern uint8_t nor_buff[1024*1024];;
#endif

extern uint8_t nor_info_block[0x100];

void SaveNor(string file = "");
bool read_nor(uint16_t addr, uint8_t &value);
bool write_nor(uint16_t addr,uint8_t value);

void init_nor();
