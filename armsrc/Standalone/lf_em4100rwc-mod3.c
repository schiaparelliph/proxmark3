//-----------------------------------------------------------------------------
// Artyom Gnatyuk, 2020
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// LF rwc   -   This mode can simulate ID from selected slot, read ID to
//              selected slot, write from selected slot to T5555 tag and store
//              readed ID to flash (only RDV4). Also you can set predefined IDs
//              in any slot.
//              To recall stored ID from flash execute:
//                  mem spifss dump o emdump p
//              or:
//                  mem spifss dump o emdump f emdump
//              then from shell:
//                  hexdump emdump -e '5/1 "%02X" /0 "\n"'
//-----------------------------------------------------------------------------
#include "standalone.h"
#include "proxmark3_arm.h"
#include "appmain.h"
#include "fpgaloader.h"
#include "lfops.h"
#include "util.h"
#include "dbprint.h"
#include "ticks.h"
#include "string.h"
#include "BigBuf.h"
#include "spiffs.h"

#ifdef WITH_FLASH
#include "flashmem.h"
#endif

#define MAX_IND 2 // 4 LEDs - 2^4 combinations
#define CLOCK 64 //for 125kHz

// low & high - array for storage IDs. Its length must be equal.
// Predefined IDs must be stored in low[].
// In high[] must be nulls
uint64_t low[] = {0,0};
uint32_t high[] = {0,0};
uint8_t *bba, slots_count;
int buflen;

void ModInfo(void) {
    DbpString(" modified LF EM4100 read/write/clone mode ");
}

uint64_t ReversQuads(uint64_t bits) {
    uint64_t result = 0;
    for (int i = 0; i < 16; i++) {
        result += ((bits >> (60 - 4 * i)) & 0xf) << (4 * i);
    }
    return result >> 24;
}

void FillBuff(uint8_t bit) {
    memset(bba + buflen, bit, CLOCK / 2);
    buflen += (CLOCK / 2);
    memset(bba + buflen, bit ^ 1, CLOCK / 2);
    buflen += (CLOCK / 2);
}

void ConstructEM410xEmulBuf(uint64_t id) {

    int i, j, binary[4], parity[4];
    buflen = 0;
    for (i = 0; i < 9; i++)
        FillBuff(1);
    parity[0] = parity[1] = parity[2] = parity[3] = 0;
    for (i = 0; i < 10; i++) {
        for (j = 3; j >= 0; j--, id /= 2)
            binary[j] = id % 2;
        for (j = 0; j < 4; j++)
            FillBuff(binary[j]);
        FillBuff(binary[0] ^ binary[1] ^ binary[2] ^ binary[3]);
        for (j = 0; j < 4; j++)
            parity[j] ^= binary[j];
    }
    for (j = 0; j < 4; j++)
        FillBuff(parity[j]);
    FillBuff(0);
}

void LED_Slot(int i) {
    LEDsoff();
    if (slots_count > 4) {
        LED(i % MAX_IND, 0); //binary indication, usefully for slots_count > 4
    } else {
        LED(1 << i, 0); //simple indication for slots_count <=4
    }
}

void FlashLEDs(uint32_t speed, uint8_t times) {
    for (int i = 0; i < times * 2; i++) {
        LED_A_INV();
        LED_B_INV();
        LED_C_INV();
        LED_D_INV();
        SpinDelay(speed);
    }
}

#ifdef WITH_FLASH
void SaveIDtoFlash(int addr, uint64_t id) {
    uint8_t bt[5];
    char *filename = "emdump";
    rdv40_spiffs_mount();
    for (int i = 0; i < 5; i++) {
        bt[4 - i] = (uint8_t)(id >> 8 * i & 0xff);
    }
    if (exists_in_spiffs(filename) == false) {
        rdv40_spiffs_write(filename, &bt[0], 5, RDV40_SPIFFS_SAFETY_NORMAL);
    } else {
        rdv40_spiffs_append(filename, &bt[0], 5, RDV40_SPIFFS_SAFETY_NORMAL);
    }
}
#endif

void RunMod() {
    StandAloneMode();
    FpgaDownloadAndGo(FPGA_BITSTREAM_LF);
    Dbprintf("[=] >> modified LF EM4100 read/write/clone started  <<");

    int selected = 0;
    //state 0 - select slot
    //      1 - read tag to selected slot,
    //      2 - simulate tag from selected slot
    //      3 - write to T5555 tag
    uint8_t state = 0;
    slots_count = sizeof(low) / sizeof(low[0]);
    Dbprintf("slots: ", slots_count);  //debug
    
    bba = BigBuf_get_addr();
    LED_Slot(selected);
    for (;;) {
        WDT_HIT();
        if (data_available()) break;
        
        int button_pressed = BUTTON_HELD(1000);
               
        SpinDelay(300);
        if (state == 0) {
            // Select mode
                Dbprintf("State=0 select slot -click to select next- hold to read slot: %x", selected);
                LED_C_ON();
               if (button_pressed == 1) {
                    // Long press - switch to simulate mode
                    DbpString("Long Press, switch to state=2 read");
                    SpinUp(100);
                    LED_Slot(selected);
                    state = 1;
                    LED_C_OFF();
                } else if (button_pressed < 0) {
                    // Click - switch to next slot
                    DbpString("Short Press, select next slot");
                    selected = (selected + 1) % slots_count;
                    LED_Slot(selected);
                    Dbprintf ("selected: %x", selected);
                }
                continue;
        }else if (state == 1) {
                // Read mode.
                LED_D_ON();
                DbpString("reading card");
                CmdEM410xdemod(1, &high[selected], &low[selected], 0);
                Dbprintf("read card: %x",high[selected]);
                FlashLEDs(100, 5);
                state = 2;
                LED_D_OFF();
                continue;
        } else if (state == 2) {
                // Simulate mode
              
                    // Click - start simulating. Click again to exit from simulate mode
                    
                    DbpString("simulating");
                    LED_Slot(selected);
                    ConstructEM410xEmulBuf(ReversQuads(low[selected]));
                    FlashLEDs(100, 5);
                    SimulateTagLowFrequency(buflen, 0, 1);
                    LED_Slot(selected);
                    state = 2; // keep simulating
        
                  if (button_pressed > 0) {
                     //Long press - switch to read mode
                   DbpString("Long Press - switch to state=3 write mode");
                    SpinDown(100);
                    LED_Slot(selected);
                    state = 3;
                  }
               continue;
        } else if (state == 3) {
                // Write tag mode
                    DbpString("State=3 write tag- click to write- hold to exit");
                    LED_C_ON();
                    LED_D_ON();
                if (button_pressed > 0) {
                    // Long press - switch to select mode
                    DbpString("Long Press- exit");
                    SpinDown(100);
                    LED_Slot(selected);
                    LEDsoff();
                    Dbprintf("data available %x", data_available);
                    //data_available = False;
                    break;
                    //state = 0;
                } else if (button_pressed < 0) {
                    // Click - write ID to tag
                    DbpString("Short Press- writing tag");
                    WriteEM410x(0, (uint32_t)(low[selected] >> 32), (uint32_t)(low[selected] & 0xffffffff));
                    LED_Slot(selected);
                    state = 3; // Switch to select mode
                    //DbpString("State=0 select mode");
                }
                continue;
        }
        
    }
}
