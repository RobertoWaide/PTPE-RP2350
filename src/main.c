
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/spi.h"
#include <stdlib.h>

#include "hardware/gpio.h"
#include "hw.h"
#include "ST7735_TFT.h"
#include "bsp/board.h"
#include "tusb.h"
#include "shared.h"
#include "menu.h"
#include "my_rtc.h"
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"


#include "sdmenu.h"
#include "usb_descriptors.h"

#include "Skylanders.h"
#include "web_server.h"

#define PicoW 0
#define BUTTON_MISC1 16
#define BUTTON_LEFT 17
#define BUTTON_SELECT 18
#define BUTTON_RIGHT 19
#define BUTTON_MISC2 20

void init_hw() {
       

  stdio_init_all();
  spi_init(SPI_PORT, 25000000);                // SPI with 1Mhz
  gpio_set_function(SPI_RX, GPIO_FUNC_SPI);
  gpio_set_function(SPI_SCK,GPIO_FUNC_SPI);
  gpio_set_function(SPI_TX, GPIO_FUNC_SPI);
  tft_spi_init();
}
void init_buttons(){
    // INIT GPIO for buttons
  gpio_init(BUTTON_MISC1);
  gpio_set_dir(BUTTON_MISC1, GPIO_IN);
  gpio_pull_up(BUTTON_MISC1);

  gpio_init(BUTTON_LEFT);
  gpio_set_dir(BUTTON_LEFT, GPIO_IN);
  gpio_pull_up(BUTTON_LEFT);

  gpio_init(BUTTON_RIGHT);
  gpio_set_dir(BUTTON_RIGHT, GPIO_IN);
  gpio_pull_up(BUTTON_RIGHT);

  gpio_init(BUTTON_SELECT);
  gpio_set_dir(BUTTON_SELECT, GPIO_IN);
  gpio_pull_up(BUTTON_SELECT);

  gpio_init(BUTTON_MISC2);
  gpio_set_dir(BUTTON_MISC2, GPIO_IN);
  gpio_pull_up(BUTTON_MISC2);
}
void init_sd(){
  //sd_card_t *pSD = sd_get_by_num(0);
  DIR dir;
  FIL fp;
  FATFS fs;
    //FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    FRESULT fr = f_mount(&fs, "", 1);
    if (FR_OK != fr)
    {
        drawError(0);
        while(true){
           if (!gpio_get(BUTTON_SELECT))
            {
                reboot();
            }    
        };
    }
    else
    {
            f_opendir(&dir, "/"); // Open Root
            f_open(&fp, "config.ptpe", FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
            f_puts("[CONFIG_WIFI];WIFI_SSID=;WIFI_PASS=;",&fp);
            f_close(&fp);
            f_closedir(&dir);
    }

    //Create Folders for PTPE to read dumps from.
    int ins = 25;
    FRESULT res = f_opendir(&dir, "/");                       /* Open SD Root */
    res = f_mkdir("Skylanders");
    if(res == FR_OK)
    {
        drawText(5, ins, "Created Skylanders Folder", ST7735_WHITE, ST7735_BLACK, 1);
        ins = ins+12;
    }
    res = f_mkdir("Dimensions");
    if(res == FR_OK)
    {
        drawText(5, ins, "Created Dimensions Folder", ST7735_WHITE, ST7735_BLACK, 1);
        ins = ins+12;
    }
    res = f_mkdir("Infinity");
    if(res == FR_OK)
    {
       
        drawText(5, ins, "Created Infinity Folder", ST7735_WHITE, ST7735_BLACK, 1);
        ins = ins+12;
    }
    f_closedir(&dir);
}
void handleWifi(){
    
}
bool StartsWith(const char *a, const char *b)
{
   if(strncmp(a, b, strlen(b)) == 0) return 1;
   return 0;
}

// ---------------------------------------------------------------------------
// main loop

int main() {
    stdio_init_all();

    // Seleciona os descritores USB do modo Skylanders.
    DeviceNum = 0;

    // Inicializa o USB antes de iniciar a rede.
    board_init();
    tud_init(0);

    // Se o Wi-Fi falhar, mant??m o USB atendido, sem travar a CPU.
    if (cyw43_arch_init() != 0) {
        printf("Erro ao inicializar o Wi-Fi CYW43.\n");
        while (true) {
            tud_task();
            tight_loop_contents();
        }
    }

    // Inicia o ponto de acesso e o servidor HTTP.
    ptpe_web_start();

    // Mant??m a emula????o USB e o processamento de rede ativos.
    while (true) {
        tud_task();
        tight_loop_contents();
    }

    return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  printf("USB: MOUNTED (DeviceNum=%d)\n", DeviceNum);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  printf("USB: UNMOUNTED (DeviceNum=%d)\n", DeviceNum);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void)remote_wakeup_en;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
  (void)buffer;
  printf("USB: GET_REPORT instance=%u id=0x%02X type=%u requested=%u\n",
         instance, report_id, (unsigned)report_type, reqlen);
  // Keep the existing behavior (STALL) until the portal protocol defines
  // the correct control-transfer response; the log makes this visible.
  return 0;
}

// Xbox 360 Traptanium reports wrap the portal payload in 0B 14.
static void portal_send_report(const uint8_t *payload, uint16_t payload_len)
{
    uint8_t report[32] = {0};
    report[0] = 0x0B;
    report[1] = 0x14;
    if (payload != NULL && payload_len > 0) {
        uint16_t copy_len = payload_len > 30 ? 30 : payload_len;
        memcpy(report + 2, payload, copy_len);
    }
    tud_hid_report(0, report, sizeof(report));
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) // Console/ Computer sends data (Query, Shutdown etc.)
{
 printf("USB: SET_REPORT instance=%u id=0x%02X type=%u len=%u",
        instance, report_id, (unsigned)report_type, bufsize);
 if (buffer != NULL) {
   unsigned int dump_len = bufsize < 32 ? bufsize : 32;
   for (unsigned int i = 0; i < dump_len; i++) printf(" %02X", buffer[i]);
 }
 printf("\n");
 if (report_type == HID_REPORT_TYPE_OUTPUT && (buffer == NULL || bufsize < 1)) {
     printf("USB: pacote OUTPUT invalido\n");
     return;
 }
 printf("%X", report_type);
 if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    const uint8_t *command_buffer = buffer;
    uint16_t command_len = bufsize;
    if (bufsize >= 3 && buffer[0] == 0x0B && buffer[1] == 0x14) {
        command_buffer = buffer + 2;
        command_len = (uint16_t)(bufsize - 2);
    }
    // char paddedbuffer[MSG_SIZE] = {0};
    char *outbuffer;
    char *midbuffer;
    FIL *curfile;
    uint actual_len = 0;

    switch (command_buffer[0]) // Commands we can recieve from the host
    {
        case 'R': // 0x52 Reboot/Shutdown Portal
            printf("Recieved reboot\n");
            outbuffer = calloc(32, 1);
            if (outbuffer == NULL) return;
            outcommand_buffer[0] = 0x52;
            outcommand_buffer[1] = 0x02;
            // Xbox 360 Traptanium Portal identification response: 52 02 1B.
            // 52 02 0A 05 08 is a different protocol response, not this portal ID.
            outcommand_buffer[2] = 0x1B;
            portal_send_report((const uint8_t *)outbuffer, 32);
            break;
        case 'J':
            printf("I do not know what it does but something related to sound");
            outbuffer = calloc(32, 1);
            if (outbuffer == NULL) return;
            outcommand_buffer[0] = command_buffer[0];
            portal_send_report((const uint8_t *)outbuffer, 32);
            break;
        case 'M':
            printf("Activate / Deactivate speaker");
            outbuffer = calloc(32, 1);
            if (outbuffer == NULL) return;
            outcommand_buffer[0] = command_buffer[0]; // We only send the command back without any return value, since we do not have a speaker
            portal_send_report((const uint8_t *)outbuffer, 32);
            break;
        case 'A': // 0x41 Activate Portal
            if (command_len < 2) return;

            printf("Recieved activate\n");
            outbuffer = calloc(32, 1);
            if (outbuffer == NULL) return;
            outcommand_buffer[0] = 0x41;
            outcommand_buffer[1] = command_buffer[1];
            outcommand_buffer[2] = 0xff;
            outcommand_buffer[3] = 0x77;
            portal_send_report((const uint8_t *)outbuffer, 32);
            break;
        case 'S': // 0x53 Sense how many Skylanders are on the Portal
            printf("Recieved sense\n");
            outbuffer = calloc(32, 1);
            if (outbuffer == NULL) return;
            outcommand_buffer[0] = 0x53;
            outcommand_buffer[1] = (web_sense_present ||
                (skylander_ram_dump_active &&
                 skylander_ram_dump_size >= SKYLANDER_BLOCK_SIZE &&
                 skylander_ram_dump_size <= sizeof(skylander_ram_dump) &&
                 skylander_ram_dump_size % SKYLANDER_BLOCK_SIZE == 0))
               ? 0x01
               : create_sense_bitmask(loaded_skylanders, MAX_SKYLANDER_COUNT);
            // pseudo: outcommand_buffer[2:5] = {0x00} (len = 3)
            outcommand_buffer[5] = sense_counter++;
            outcommand_buffer[6] = 0x01;
            portal_send_report((const uint8_t *)outbuffer, 32);
            // free(outbuffer);
            break;
        case 'C': // 0x42 Skylander Portal Color
            printf("Color ");
            portal_send_report(command_buffer, command_len);
            return;
            // tud_hid_report(0, buffer, bufsize);
            break;
        case 'Q': // 0x51 Read Blocks (16 Bytes) From Skylander
            if (command_len < 3) return;
            printf("Recieved query\n");
            outbuffer = calloc(SKYLANDER_MSG_SIZE, 1);
            if (outbuffer == NULL) { return; }
            outcommand_buffer[0] = 0x51;
            outcommand_buffer[1] = 0x10;
            if(command_buffer[1] == 0x10 || command_buffer[1] == 0x20 || command_buffer[1] == 0x30 || command_buffer[1] == 0x40 || command_buffer[1] == 0x50 || command_buffer[1] == 0x60 || command_buffer[1] == 0x70 || command_buffer[1] == 0x80 || command_buffer[1] == 0x90 || command_buffer[1] == 0xA0 || command_buffer[1] == 0xB0 || command_buffer[1] == 0xC0 || command_buffer[1] == 0xD0 || command_buffer[1] == 0xE0 || command_buffer[1] == 0xF0)
            {
                outcommand_buffer[1] = 0x10;
            }
            if(command_buffer[1] == 0x11 || command_buffer[1] == 0x21 || command_buffer[1] == 0x31 || command_buffer[1] == 0x41 || command_buffer[1] == 0x51 || command_buffer[1] == 0x61 || command_buffer[1] == 0x71 || command_buffer[1] == 0x81 || command_buffer[1] == 0x91 || command_buffer[1] == 0xA1 || command_buffer[1] == 0xB1 || command_buffer[1] == 0xC1 || command_buffer[1] == 0xD1 || command_buffer[1] == 0xE1 || command_buffer[1] == 0xF1)
            {
                outcommand_buffer[1] = 0x11;
            }
            if (command_buffer[1] == 0x12 || command_buffer[1] == 0x22 || command_buffer[1] == 0x32 || command_buffer[1] == 0x42 || command_buffer[1] == 0x52 || command_buffer[1] == 0x62 || command_buffer[1] == 0x72 || command_buffer[1] == 0x82 || command_buffer[1] == 0x92 || command_buffer[1] == 0xA2 || command_buffer[1] == 0xB2 || command_buffer[1] == 0xC2 || command_buffer[1] == 0xD2 || command_buffer[1] == 0xE2 || command_buffer[1] == 0xF2)
            {
                outcommand_buffer[1] = 0x12;
            }
      
            outcommand_buffer[2] = command_buffer[2];

            midbuffer = calloc(SKYLANDER_BLOCK_SIZE, 1);
            if (midbuffer == NULL) {
                free(outbuffer);
                return;
            }

            if (skylander_ram_dump_active) {
                if (skylander_ram_dump_size < SKYLANDER_BLOCK_SIZE ||
                    skylander_ram_dump_size > sizeof(skylander_ram_dump) ||
                    skylander_ram_dump_size % SKYLANDER_BLOCK_SIZE != 0) {
                    free(midbuffer);
                    free(outbuffer);
                    return;
                }

                unsigned int offset = (unsigned int)command_buffer[2] * SKYLANDER_BLOCK_SIZE;
                if (offset + SKYLANDER_BLOCK_SIZE > skylander_ram_dump_size) {
                    free(midbuffer);
                    free(outbuffer);
                    return;
                }
                memcpy(midbuffer, skylander_ram_dump + offset, SKYLANDER_BLOCK_SIZE);
            } else {
                curfile = loaded_skylanders[0];
                if (curfile == NULL) {
                    free(midbuffer);
                    free(outbuffer);
                    return;
                }
                FRESULT fr = f_lseek(curfile, (FSIZE_t)command_buffer[2] * SKYLANDER_BLOCK_SIZE);
                if (fr != FR_OK) {
                    free(midbuffer);
                    free(outbuffer);
                    return;
                }
                UINT actual_len = 0;
                fr = f_read(curfile, midbuffer, SKYLANDER_BLOCK_SIZE, &actual_len);
                if (fr != FR_OK || actual_len != SKYLANDER_BLOCK_SIZE) {
                    free(midbuffer);
                    free(outbuffer);
                    return;
                }
            }

            memcpy(outbuffer + 3, midbuffer, SKYLANDER_BLOCK_SIZE);
            free(midbuffer);
portal_send_report((const uint8_t *)outbuffer, SKYLANDER_MSG_SIZE);
            break;
        default:
            return;
    }

    printf("Sent: ");
    for (unsigned int i = 0; i < SKYLANDER_MSG_SIZE; i++) { printf("%02X", (unsigned char)outcommand_buffer[i]); }
    printf("\n");
    free(outbuffer);
  }
  
}
