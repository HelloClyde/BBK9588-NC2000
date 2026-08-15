#include "nc2000.h"
#include "comm.h"
#include "ram.h"
#include "state.h"
#include "cpu.h"
#include "mem.h"
#include "io.h"
#include "rom.h"
#include "nor.h"
#include "nand.h"
#ifndef BBK9588
#include <SDL2/SDL.h>
#include <SDL_timer.h>
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#ifndef BBK9588
#include <deque>
#endif
#include "sound.h"
#ifndef BBK9588
#include "compare/c6502.h"
#include "console.h"
#endif
#include "iv_uart.h"
#ifdef BBK9588
#include "platform/bbk9588/diagnostic_log.h"
#endif
#ifndef BBK9588
#include "cmd.h"
#include "key_new.h"
#else
extern bool console_on;
void clear_cmds();
#endif
extern WqxRom nc2k_rom;

nc2k_states_t nc2k_states;

void save_state(string file_name){
	if(file_name.empty()) file_name=nc2k_rom.statesPath;
	else file_name+=".state";
	FILE* file = fopen(file_name.c_str(), "wb");
	if (file == NULL) {
		printf("states file %s open failed, skip saving!\n", nc2k_rom.statesPath.c_str());
		return;
	}
	fwrite(&nc2k_states.SAVE_STATE_BEGIN, 1, &nc2k_states.SAVE_STATE_END-&nc2k_states.SAVE_STATE_BEGIN, file);
	fflush(file);
	fclose(file);
	printf("state saved to file %s!!\n",file_name.c_str());
}

void delete_state(string file_name){
	if(file_name.empty()) file_name=nc2k_rom.statesPath;
	else file_name+=".state";
	if(remove(file_name.c_str())==0){
		printf("state file %s deleted!\n",file_name.c_str());
	}else{
		printf("state file %s not exist or no permission to delete!\n",file_name.c_str());
	}
}

void load_state(){
	FILE* file = fopen(nc2k_rom.statesPath.c_str(), "rb");
	if (file == NULL) {
		printf("states file %s open failed, skip loading!\n", nc2k_rom.statesPath.c_str());
		return;
	}
	int ret=fread(&nc2k_states.SAVE_STATE_BEGIN, 1, &nc2k_states.SAVE_STATE_END-&nc2k_states.SAVE_STATE_BEGIN, file);
	fclose(file);
	printf("loaded states from %s, ret=%d\n", nc2k_rom.statesPath.c_str(),ret);
	//super_switch();
}

void LoadNC2k(){
#ifdef BBK9588
	bbk_diag_log_printf("LOAD_STATE_INIT_BEGIN");
#endif
	nc2k_states.init(); //fix re-run issue on emscripten version
	clear_cmds();
	clear_nand_status();
	clear_iv();

#ifndef BBK9588
	init_keyitems();
#endif

#ifndef BBK9588
	init_io(); //for old io implemet only
#endif
	
	void CreateHotlinkMapping();
	CreateHotlinkMapping();

#ifdef BBK9588
	bbk_diag_log_printf("LOAD_NOR_BEGIN");
#endif
	init_nor();
#ifdef BBK9588
	bbk_diag_log_printf("LOAD_NOR_DONE");
#endif
	if(pc1000mode||nc1020mode) {
		init_rom();
	}
	if(nc2000mode||nc3000mode) {
#ifdef BBK9588
		bbk_diag_log_printf("LOAD_NAND0_BEGIN");
#endif
		read_nand0_file();
#ifdef BBK9588
		bbk_diag_log_printf("LOAD_NAND0_DONE");
		bbk_diag_log_printf("LOAD_NAND_BEGIN");
#endif
		read_nand_file();
#ifdef BBK9588
		bbk_diag_log_printf("LOAD_NAND_DONE");
#endif
	}

#ifdef BBK9588
	bbk_diag_log_printf("LOAD_MEMORY_BEGIN");
#endif
	init_mem();

	if(nc1020mode){
		ram_io[0x0b]=0x01;
	}

	//reset_cpu_states();
	initalize_illegal_op_tables();
	init_cpu_new();
#ifdef BBK9588
	bbk_diag_log_printf("LOAD_CPU_DONE pc=%04X", (unsigned)mPC & 0xffffu);
#endif

	if(nc2000mode||nc3000mode){
		//nc3000c-lee has it but seems like no need?
		//ram_io[0x18]=0x20;
	}

	if(enable_load_state){
		load_state();
		if(nc2000mode){
			void sync_time_2000();
			if(enable_auto_time_sync) sync_time_2000();
		}
		if(nc1020mode){
			void sync_time_1020();
			if(enable_auto_time_sync) sync_time_1020();
		}
	}

	super_switch();
#ifdef BBK9588
	bbk_diag_log_printf("LOAD_SWITCH_DONE pc=%04X", (unsigned)mPC & 0xffffu);
#endif

	if(enable_load_state&&reset_after_load_state){
		void set_warm_reset_flag();
		set_warm_reset_flag();
		void warm_reset();
		warm_reset();
	}
}

void SaveNC2kIfNeed(){
    if(save_flash_on_exit){
#ifdef BBK9588
      bbk_diag_log_printf("SAVE_FLASH_REQUESTED");
#endif
      save_flash("");
    }
    if(save_state_on_exit){
      save_state("");
    }
}

static unsigned short &lcdbuffaddr = nc2k_states.lcdbuffaddr;
static unsigned short &lcdbuffaddrmask = nc2k_states.lcdbuffaddrmask;
bool is_grey_mode(){
	if(console_on) return false;

	unsigned short lcd_addr = lcdbuffaddr&lcdbuffaddrmask;
	//printf("lcdaddr=%x\n",lcd_addr);
	//fflush(stdout);
	if(nc2000mode||nc3000mode||nc1020mode)
		return lcd_addr==0x1380;
	return false;
}
bool CopyLcdBuffer(uint8_t* buffer){
    unsigned short lcd_addr = lcdbuffaddr&lcdbuffaddrmask;
	if (lcd_addr == 0) return false;

	if(nc1020mode){
		if(!is_grey_mode()){
			memcpy(buffer, ram_buff + lcd_addr, 1600 );
		}else{
			memcpy(buffer, ram_buff + lcd_addr, 1600 *2);
		}
		return true;;
	}
	else if(nc2000mode||nc3000mode){
		if(!is_grey_mode()){
			//TODO: cannot use lcd_addr, it has some offset
			//// 应该是lcdaddr io哪里没有模拟好。 如果lcd end addr不是(1fff,0fff,...)可能应该忽略lcd_addr的设置
			memcpy(buffer, ram_buff + 0x19c0, 1600 );
		}else{
			memcpy(buffer, ram_buff + lcd_addr, 1600 *2);
		}
		return true;
	}else{
		memcpy(buffer, ram_buff + lcd_addr, 1600);
		return true;
	}
	assert(false);
}

void RunTimeSlice(uint32_t time_slice) {
	uint32_t new_cycles = time_slice * CYCLES_MS;

	if(!fast_forward) {
		new_cycles= new_cycles * speed_multiplier;
	}else if(fast_forward_limit==0){
		new_cycles= new_cycles*1;
	}else{
		new_cycles= new_cycles * fast_forward_limit;
	}

	u64_t target_cycles=nc2k_states.cycles +new_cycles;

	while (nc2k_states.cycles < target_cycles) {
		if(cpu_loop_version == CPU_RUN1){
			cpu_run();
		}else if (cpu_loop_version == CPU_RUN2){
			cpu_run2();
		}else if (cpu_loop_version == CPU_RUN3){
			cpu_run3();
		}else{
			assert(false);
		}
		post_cpu_run_sound_handling();
	}
}

void save_flash(string file){
#ifdef BBK9588
	bbk_diag_log_printf("SAVE_NAND0_BEGIN");
#endif
	write_nand0_file(file);
#ifdef BBK9588
	bbk_diag_log_printf("SAVE_NAND0_DONE");
	bbk_diag_log_printf("SAVE_NAND_BEGIN");
#endif
	write_nand_file(file);
#ifdef BBK9588
	bbk_diag_log_printf("SAVE_NAND_DONE");
	bbk_diag_log_printf("SAVE_NOR_BEGIN");
#endif
	SaveNor(file);
#ifdef BBK9588
	bbk_diag_log_printf("SAVE_NOR_DONE");
#endif
	printf("flash saved to file!!\n");
}

void nc2k_warm_reset(){

	uint8_t* ioReg=nc2k_states.ram_io;

	const int simple_warm_reset=true;
  if(simple_warm_reset){
	ioReg[0x05] &=0x1f;  //reset cks
	nc2k_states.speed_scaledown=1;
  } else {
    //0x00
    ioReg[0x00]=0;

    //0x01
    nc2k_states.inner_interrupt_control&=0xfc; //TMBIE TMAIE clear
    
    //0x02
    ////ioReg[0x02]=0;          //if reset both 0x02 and 0x03, on/off key will trigger cold reset
    
    //0x03
    ////ioReg[0x03]=0;

    //0x04
    nc2k_states.w04_b03_TBC &=0xf0;
    
    //0x05
    ioReg[0x05] &=0x1f;
    ioReg[0x05] &=0xf7;
    nc2k_states.lcdon=0;
    nc2k_states.speed_scaledown=1;

    //0x0a
    ioReg[0x0a] &=0xe0;
    
    //0x0b
    ioReg[0x0b]&=0xfd;
    nc2k_states.lcden=0;

    //0x0c
    ioReg[0x0c]&=0xfc;
    nc2k_states.w0c_b67_TMODESL =0;
    nc2k_states.w0c_b45_TM0S =0;
    nc2k_states.w0c_b23_TM1S = 0;
    nc2k_states.w0c_b345_TMS = 0;

    //0x0d
    ioReg[0x0d]&=0xf8;

    //0x14
    ioReg[0x14]=0;

    //0x19
    ioReg[0x19]=0xef;

    //0x1a
    ioReg[0x1a]=0;

    //0x1b
    ioReg[0x1b]=0;

    //0x1c
    ioReg[0x1c]&=0xbf;

    //0x1e
    ioReg[0x1e]=0;
  }

    super_switch();
}

void nc2k_cold_reset(){
    clear_cmds();
    clear_nand_status();
    clear_iv();//if this is put into warm_reset, alarm wakeup will not work correcly

    nc2k_warm_reset();

    //memset(ram_io,0,sizeof(nc2k_states.ram_io));
    //memset(ext_reg, 0, sizeof(nc2k_states.ext_reg));
    nc2k_states.reset();
    super_switch();
}
