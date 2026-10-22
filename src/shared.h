#include "f_util.h"
#include "ff.h"
#define MAX_SKYLANDER_COUNT 4
#define SKYLANDER_MSG_SIZE 32
#define SKYLANDER_BLOCK_SIZE 16
extern int DeviceNum;
extern int WifiAllowed;
extern int Selected;
extern int Connected;
extern int SDLines;
extern int currSel;
extern int maxSel;
extern int SkylanderCount;
extern char sense_counter;
extern int first_init;
extern FIL *loaded_skylanders[MAX_SKYLANDER_COUNT];

void reboot();


// Dump de Skylander carregado em RAM pela interface web.
extern unsigned char skylander_ram_dump[2048];
extern unsigned int skylander_ram_dump_size;
extern int skylander_ram_dump_active;
