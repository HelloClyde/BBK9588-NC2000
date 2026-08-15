
#include "ansi/w65c02.h"
#include "comm.h"
#include "state.h"
#include <cassert>
#include <cstdio>
#include "nand.h"
extern WqxRom nc2k_rom;
extern nc2k_states_t nc2k_states;
static uint8_t* ram_buff = nc2k_states.ram;
static uint8_t* ram_io = nc2k_states.ram_io;

#ifdef BBK9588
template <unsigned Capacity>
class BbkByteQueue {
public:
    BbkByteQueue() : count_(0) {}
    void clear() { count_ = 0; }
    bool empty() const { return count_ == 0; }
    unsigned size() const { return count_; }
    void push_back(uint8_t value) {
        if (count_ < Capacity) values_[count_++] = value;
    }
    uint8_t &operator[](unsigned index) { return values_[index]; }
    const uint8_t &operator[](unsigned index) const { return values_[index]; }
private:
    uint8_t values_[Capacity];
    unsigned count_;
};
static BbkByteQueue<8> nand_cmd;
static BbkByteQueue<8> nand_addr;
static BbkByteQueue<544> nand_data;
#else
static deque<uint8_t> nand_cmd;
static deque<uint8_t> nand_addr;
static deque<uint8_t> nand_data;
#endif

static int nand_read_cnt=0;
//char nand_ori[65536*2][512];
#ifdef BBK9588
static uint8_t nand0_pages[64][528];
static uint8_t nand_cache[528];
static uint32_t nand_cache_page = 0xffffffffu;
static bool nand_cache_dirty;
static FILE *nand_file;
#else
static char nand[65536*2+64][528];
#endif
//char nand_spare[65536+64][16];

char nand_magic[11];

#ifdef BBK9588
static bool flush_nand_cache()
{
    if (!nand_cache_dirty || nand_cache_page < 64u || !nand_file) return true;
    uint32_t offset = (nand_cache_page - 64u) * 528u;
    if (fseek(nand_file, (long)offset, SEEK_SET) != 0) return false;
    if (fwrite(nand_cache, 1u, sizeof(nand_cache), nand_file) != sizeof(nand_cache)) return false;
    fflush(nand_file);
    nand_cache_dirty = false;
    return true;
}

static bool load_nand_cache(uint32_t page)
{
    if (page == nand_cache_page) return true;
    if (!flush_nand_cache() || page < 64u || !nand_file) return false;
    uint32_t offset = (page - 64u) * 528u;
    if (fseek(nand_file, (long)offset, SEEK_SET) != 0) return false;
    memset(nand_cache, 0xff, sizeof(nand_cache));
    if (fread(nand_cache, 1u, sizeof(nand_cache), nand_file) != sizeof(nand_cache)) return false;
    nand_cache_page = page;
    nand_cache_dirty = false;
    return true;
}

static uint8_t nand_storage_read(uint32_t offset)
{
    uint32_t page = offset / 528u;
    uint32_t column = offset % 528u;
    if (page < 64u) return nand0_pages[page][column];
    if (!load_nand_cache(page)) return 0xffu;
    return nand_cache[column];
}

static void nand_storage_program(uint32_t offset, uint8_t value)
{
    uint32_t page = offset / 528u;
    uint32_t column = offset % 528u;
    uint8_t *target;
    if (page < 64u) target = &nand0_pages[page][column];
    else {
        if (!load_nand_cache(page)) return;
        target = &nand_cache[column];
        nand_cache_dirty = true;
    }
    if (*target != 0xffu && forced_erase_before_write) *target = 0xffu;
    *target &= value;
}

static void nand_storage_erase(uint32_t offset, uint32_t length)
{
    while (length != 0u) {
        uint32_t page = offset / 528u;
        uint32_t column = offset % 528u;
        uint32_t chunk = 528u - column;
        if (chunk > length) chunk = length;
        if (page < 64u) memset(&nand0_pages[page][column], 0xff, chunk);
        else if (load_nand_cache(page)) {
            memset(&nand_cache[column], 0xff, chunk);
            nand_cache_dirty = true;
        }
        offset += chunk;
        length -= chunk;
    }
}
#else
static uint8_t nand_storage_read(uint32_t offset)
{
    return ((uint8_t *)&nand[0][0])[offset];
}

static void nand_storage_program(uint32_t offset, uint8_t value)
{
    uint8_t *target = &((uint8_t *)&nand[0][0])[offset];
    if (*target != 0xffu && forced_erase_before_write) *target = 0xffu;
    *target &= value;
}

static void nand_storage_erase(uint32_t offset, uint32_t length)
{
    memset((uint8_t *)&nand[0][0] + offset, 0xff, length);
}
#endif

void read_nand0_file(){
#ifdef BBK9588
    memset(nand0_pages, 0xff, sizeof(nand0_pages));
    char *p0 = (char *)&nand0_pages[0][0];
#else
    memset(nand,0xff, 64*528);
    char *p0= &nand[0][0];
#endif
    FILE *f = fopen(nc2k_rom.nand0Path.c_str(), "rb");
    if(f==0) {
        printf("file %s not exist!\n",nc2k_rom.nand0Path.c_str());
        exit(-1);
    }
    fseek(f, 0, SEEK_END);
    long long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */
    assert(fsize<= 64*528);
    fread(p0, fsize, 1, f);
    fclose(f);
    printf("<nand0_file_size=%llu>\n",fsize);
    for(int i=0;i<sizeof(nand_magic)-1;i++){
        nand_magic[i]=p0[0x200+0x10+i];
    }
    nand_magic[sizeof(nand_magic)-1]=0;
    printf("nand magic: %s\n",nand_magic);
}

void read_nand_file(){
#ifdef BBK9588
    nand_cache_page = 0xffffffffu;
    nand_cache_dirty = false;
    nand_file = fopen(nc2k_rom.nandFlashPath.c_str(), "rb+");
    if (!nand_file) nand_file = fopen(nc2k_rom.nandFlashPath.c_str(), "r+b");
    if (!nand_file) {
        printf("file %s not exist or is read-only!\n", nc2k_rom.nandFlashPath.c_str());
        exit(-1);
    }
    fseek(nand_file, 0, SEEK_END);
    long long fsize = ftell(nand_file);
    fseek(nand_file, 0, SEEK_SET);
    if (fsize < (long long)num_nand_pages * 528ll) exit(-1);
    printf("<nand_file_size=%llu>\n", fsize);
#else
    char *p0= &nand[64][0];
    memset(p0,0xff,sizeof(nand)-64*528);
    FILE *f = fopen(nc2k_rom.nandFlashPath.c_str(), "rb");
    if(f==0) {
        printf("file %s not exist!\n",nc2k_rom.nandFlashPath.c_str());
        exit(-1);
    }
    fseek(f, 0, SEEK_END);
    long long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);  /* same as rewind(f); */
    assert(fsize + 64*528 <= (int)sizeof(nand));
    fread(p0, fsize, 1, f);
    fclose(f);
    printf("<nand_file_size=%llu>\n",fsize);
#endif

#if 0
    if(nc2000mode){
        if(!nc2000_use_2600_rom){
            //if it's not 2600 rom, then it should always be this
            memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc2000",strlen("ggv nc2000"));
        }
        else{
            if(!nc2600_rom_use_ggvsim){
                //2600 physical nor expect this to be "ggv nc2010"
                //nc2kutil dump shows there is a '\n' or 0x0A. But this doesn't matter. It boots either with or without `\n`
                memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc2010\n",strlen("ggv nc2010\n"));
            }else{
                memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc2000",strlen("ggv nc2000"));
            }
        }
    }
    if(nc3000mode){
        memcpy(&nand[0][0]+0x200+0x10 /*512+16=528*/,"ggv nc3000",strlen("ggv nc3000"));
    }
#endif

}

void write_nand0_file(string file){
    if(!nc2000mode &&!nc3000mode) return;
    if(file.empty()) file=nc2k_rom.nand0Path;
    else file+=".nand0";
     FILE *f = fopen(file.c_str(), "wb");
#ifdef BBK9588
    if (!f) return;
    fwrite(&nand0_pages[0][0], 64*528 , 1 , f);
#else
    fwrite(&nand[0][0], 64*528 , 1 , f);
#endif
    fclose(f);
}

void write_nand_file(string file){
    if(!nc2000mode &&!nc3000mode) return;
    if(file.empty()) file=nc2k_rom.nandFlashPath;
    else file+=".nand";
#ifdef BBK9588
    (void)file;
    (void)flush_nand_cache();
    if (nand_file) fclose(nand_file);
    nand_file = 0;
    nand_cache_page = 0xffffffffu;
#else
    FILE *f = fopen(file.c_str(), "wb");
    assert(num_nand_pages*528 + 64*528 <= sizeof(nand));
    fwrite(&nand[0][0]+ 64*528, num_nand_pages*528, 1 , f);
    fclose(f);
#endif
}
void clear_nand_status(){
    nand_cmd.clear();
    nand_data.clear();
    nand_addr.clear();
    nand_read_cnt = 0;
}
uint8_t read_nand(){
    bool CLE;
    bool ALE;
    bool CE;
    if(nc3000mode){
        CLE = ram_io[0x18]&0x20;
        ALE = ram_io[0x18]&0x10;
        CE = ram_io[0x18]&0x04;
        if(CE) {
            if(debug_level>=1) printf("read while no CE\n");
        }
    }
    if(nc2000mode){
        CLE = ram_io[0x18]&0x01;
        ALE = ram_io[0x18]&0x02;
        CE = ram_io[0x18]&0x40;
        if(CE) {
            if(debug_level>=1) printf("read while no CE\n");
        }
    }
    if(CLE && ALE){
        if(debug_level>=1) printf("oops, in nand read, both CLE and ALE true!\n");
    }

    //printf("tick=%lld, read %x  %02x\n",tick, addr, ram_io[addr]);
    uint8_t roa_bbs=ram_io[0x0a];
    uint8_t ramb_vol=ram_io[0x0d];
    uint8_t bs=ram_io[0x00];
   ///////// uint16_t p=nc1020_states.cpu.reg_pc-4;

     if(enable_debug_nand) printf("tick=%llu read $29\n",tick%10000);

    if(nand_cmd.size()==0) {
        if(debug_level>=1) printf("oops! no nand cmd %d %d %d\n",CLE,ALE,CE);
        return 0xff;
    }
    assert(nand_cmd.size()>0);

    /*
        special handle of read status after a long time
    */
    if(nand_cmd[0]==0x70 && nand_cmd.size()==1 && nand_addr.size()==0 &&nand_data.size()==0) {
        clear_nand_status();
        return 0x40;
    }

    if(nand_cmd[0]==0x90 &&nand_cmd.size()==1 && nand_addr.size()==1 && nand_addr[0]==0x00 &&nand_data.size()==0) {
        if(nand_read_cnt==0) {
            nand_read_cnt++;
            return 0xec;
        }
        if(nand_read_cnt==1) {
            clear_nand_status();
            return 0x75;
        }
        assert(false);
        return 0;
    }

    /*
        robust check
    */
    if(nand_cmd[0]!=0x0 && nand_cmd[0]!=0x1 &&nand_cmd[0]!=0x60 &&nand_cmd[0]!=0x50){
        printf("<<%x>>!!!\n",(unsigned char)nand_cmd[0]);
        for(int i=0;i<nand_cmd.size();i++){
            printf("<%x>",(unsigned char)nand_cmd[i]);
        }
        printf("\n");
        assert(false);
        return 0;
    }

    /*
        read low/high and read spare
    */
    unsigned char cmd=nand_cmd[0];
    if(cmd ==0 ||cmd==1||cmd==0x50){
        if(nand_cmd.size()!=1 || nand_addr.size()!=4  ||nand_data.size()!=0){
            printf("oops cmd size!=5\n");
            for(int i=0;i<nand_cmd.size();i++){
                printf("<%x>",(unsigned char)nand_cmd[i]);
            }
            printf("\n");

            /*if(nand_cmd.size()==1 && nand_cmd[0]==0x0){
                printf("oops!! nand_cmd=[0] but trying to read\n");
                return 0xff;
            }*/
            assert(false);
            return 0xff;
        }

        assert(nand_cmd.size()==1 && nand_addr.size()==4 && nand_data.size()==0);
        unsigned char low=nand_addr[0];
        unsigned char mid=nand_addr[1];
        unsigned char high=nand_addr[2];
        unsigned char a25=nand_addr[3]&0x01;

        uint32_t pos=a25*256u*256u+   high*256u+mid;

        /*
        if(nand_cmd.size()==5&&false){
            printf("[%x %x]",low,high);
            
            for(int i=0;i<nand_cmd.size();i++){
                printf("<%x>",(unsigned char)nand_cmd[i]);
            }
            printf("\n");
            exit(-1);
            //printf("<%x;%x,%x:%x,%d>", final, pos, low,cmd,nand_read_cnt);
        }*/
        
        unsigned int x=pos;
        unsigned int y=low;
        //unsigned int pre_final= pos*528u+ y +nand_read_cnt;
        //assert(pre_final%528==0);
        if(cmd==0x1) y+=256u;
        if(cmd==0x50) y+=512u;
        unsigned int final= pos*528u+ y +nand_read_cnt;
        if(nand_read_cnt!=0||cmd!=0){
            //assert(final%528!=0);
            if(final%528==0) if(debug_level>=1) printf("warn: read %04x accross 528 boundary\n",final);
        }


        //final-=32*1024;
        if(nand_read_cnt==0 && enable_debug_nand){
            printf("[%x %x]",low,high);
            
            for(int i=0;i<nand_cmd.size();i++){
                printf("<%x>",(unsigned char)nand_cmd[i]);
            }
            printf("<%x;%x,%x:%x,%d>\n", final, pos, low,cmd,nand_read_cnt);
        }
        uint8_t result=nand_storage_read(final);
        //if(final<0) return 0x00;
        //uint8_t result=nand[pos][low+off+nand_read_cnt];
        nand_read_cnt++;
        //printf("<<%02x>>",result);
        return result;
    }

    assert(false);
    return 0xff;
}

void debug_show_nand_cmd(){
    if(enable_debug_nand)
    {
        for(int i=0;i<nand_cmd.size();i++){
            printf("<%02x>",(unsigned char)nand_cmd[i]);
        }
        printf("\n");
    }
}
void nand_write(uint8_t value){
    bool CLE;
    bool ALE;
    bool CE;
    if(nc3000mode){
        CLE = ram_io[0x18]&0x20;
        ALE = ram_io[0x18]&0x10;
        CE = ram_io[0x18]&0x04;
    }
    if(nc2000mode){
        CLE = ram_io[0x18]&0x01;
        ALE = ram_io[0x18]&0x02;
        CE = ram_io[0x18]&0x40;
    }
    if(CLE && ALE){
        if(debug_level>=1) printf("oops, in nand write, both CLE and ALE true!\n");
        return;
    }

    //printf("tick=%llu write $29 %x  CLE=%d ALE=%d %d\n",tick%10000,value,CLE,ALE,(int)nand_cmd.size());
    if(enable_debug_nand) printf("tick=%llu write $29 %x  CLE=%d ALE=%d\n",tick%10000,value,CLE,ALE);
    //printf("tick=%lld, write %x  %02x\n",tick, addr, value);
    uint8_t roa_bbs=ram_io[0x0a];
    uint8_t ramb_vol=ram_io[0x0d];
    uint8_t bs=ram_io[0x00];

    if(CLE){
        //note: the datasheet says 0xff doesn't need CLE, but in wqx code seems like CLE is always enabled when 0xff is used
        if(value ==0xff || value == 0x00|| value==0x01 || value ==0x50 ||value==0x60||value ==0x70||value==0x90){
            debug_show_nand_cmd();
            if(nand_cmd.size()>0){
                if(nand_cmd.size()==1 && nand_addr.size()==4 && nand_data.size()==0) assert(nand_cmd[0]==0x00||nand_cmd[0]==0x01||nand_cmd[0]==0x50);
                else if(nand_cmd.size()==2 && nand_addr.size()==3 &&nand_data.size()==0) assert(nand_cmd[0]==0x60);
                else assert(false);
            }
            clear_nand_status();
            if(value!=0xff){
                nand_cmd.push_back(value);
            }
            goto out;
        }
        if(value ==0x10) {
            if(nand_cmd[0]==0x50 && nand_cmd.size()== 2&& nand_addr.size()==4 && nand_data.size()==16) {
                assert(nand_cmd[1]==0x80);

                unsigned char low=nand_addr[0];
                unsigned char mid=nand_addr[1];
                unsigned char high=nand_addr[2];
                unsigned char a25=nand_addr[3]&0x01;

                uint32_t pos=a25*256u*256u+high*256u+mid;

                unsigned int x=pos;
                unsigned int y=low;
                unsigned int final= pos*528u+ y +512;

                assert((final-512)%(528)==0);

                bool warn=false;
                for(int i=0;i<16;i++){
                    if(nand_storage_read(final+i)!=0xff){
                        warn=true;
                    }
                    nand_storage_program(final+i, nand_data[i]);
                }
                if(warn){
                    if(debug_level>=1) printf("oops writing to non-erased byte at %x!!!!!!!!!!\n",final);
                }
                printf("[nand] program spare, offset=%x\n",final);
                clear_nand_status();
            }
            else if(nand_cmd[0]==0x0 && nand_cmd.size()==2 && nand_addr.size()==4 && nand_data.size()==528){
                assert(nand_cmd[1]==0x80);

                unsigned char low=nand_addr[0];
                unsigned char mid=nand_addr[1];
                unsigned char high=nand_addr[2];
                unsigned char a25=nand_addr[3]&0x01;

                uint32_t pos=a25*256u*256u+high*256u+mid;

                unsigned int x=pos;
                unsigned int y=low;
                unsigned int final= pos*528u+ y;
                assert(final%(528)==0);
                printf("[nand] program, offset=%x\n",final);

                bool warn=false;
                for(int i=0;i<528;i++){
                    if(nand_storage_read(final+i)!=0xff){
                        warn=true;
                    }
                    nand_storage_program(final+i, nand_data[i]);
                }
                if(warn){
                    if(debug_level>=1) printf("oops writing to non-erased byte at %x!!!!!!!!!!\n",final);
                }
                clear_nand_status();
            }
            else{
                debug_show_nand_cmd();
                printf("unexpected situation for cmd 0x10 %d",(int)nand_cmd.size());
                assert(false);
            }
            goto out;
        }
        if(value==0xd0||value==0x80){
            if(value==0xd0){
                assert(nand_cmd.size()==1);
                assert(nand_cmd[0]==0x60);
                assert(nand_addr.size()==3);
                assert(nand_data.size()==0);

                unsigned char low=nand_addr[0];
                unsigned char mid=nand_addr[1];
                unsigned char high=nand_addr[2]&0x01;

                unsigned int final= (high*256u*256u + mid*256u+low)*528u;

                nand_read_cnt++;
                printf("[nand] erase, offset=%x\n",final);

                assert(final%(32*528)==0);
#ifndef BBK9588
                assert(final +32*528 <= sizeof(nand));
#endif
                nand_storage_erase(final,32*528);
            }
            if(value==0x80){
                assert(nand_cmd.size()>=1);
                assert(nand_cmd[0]==0x50||nand_cmd[0]==0x00);
                assert(nand_addr.size()==0);
                assert(nand_data.size()==0);
            }
            nand_cmd.push_back(value);
            goto out;
        }
        assert(false);
    }

    if(ALE){
        if(nand_cmd.size()==0){
            printf("got addr %02x while nand_cmd is empty\n",value);
            assert(false);
        }

        nand_addr.push_back(value);
        goto out;
    }
 

    if(nand_cmd.size()!=0) {
        if(nand_cmd[0]==0x50) {
            assert(nand_cmd.size()>=2);
            assert(nand_cmd[1]==0x80);
            assert(nand_cmd.size()<22);
        }else if (nand_cmd[0]==0x00){
            assert(nand_cmd.size()>=2);
            assert(nand_cmd[1]==0x80);
            assert(nand_cmd.size()<534);
        }else{
            assert(false);
        }
        nand_data.push_back(value);
    }
    else{
        for(int i=0;i<nand_cmd.size();i++){
            printf("<%02x>",nand_cmd[i]);
        }
        printf("[%02x]\n",value);
        printf("got data %02x while nand_cmd is empty\n",value);
        assert(false);
    }
    goto out;

    out:;

    // the out label here is for put some print cmd for debug

    //if(nand_cmd.size()==1&& nand_cmd[0]==0xff) nand_cmd.clear();
    //printf("bs=%x roa_bbs=%x pc=%x  %x %x %x %x \n",ram_io[0x00], ram_io[0x0a], reg_pc,  Peek16(p), Peek16(p+1),Peek16(p+2),Peek16(p+3));
    //if(do_inject) wanna_inject=true;
}
