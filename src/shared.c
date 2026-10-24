#include "shared.h"
#include "menu.h"
#include "f_util.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"


char sense_counter = 0;
int DeviceNum = 0;
int WifiAllowed = 0;
int Selected = 0;
int Connected = 0;
int SDLines = 25;
int currSel = 0;
int maxSel = 2;
int SkylanderCount = 0;
int first_init = 0;
FIL *loaded_skylanders[MAX_SKYLANDER_COUNT] = {0};
unsigned char skylander_ram_dump[2048] = {0};
unsigned int skylander_ram_dump_size = 0;
int skylander_ram_dump_active = 0;
volatile int web_sense_present = 0;


void reboot(){
    clearScreen();
    sleep_ms(100);
    watchdog_enable(1, 1);
    watchdog_reboot(0,0,1);
    while(1);
}

