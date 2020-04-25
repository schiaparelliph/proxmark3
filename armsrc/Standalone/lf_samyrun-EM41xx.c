//-----------------------------------------------------------------------------
// Samy Kamkar, 2012
// Christian Herrmann, 2017
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// main code for LF aka SamyRun by Samy Kamkar
//-----------------------------------------------------------------------------
#include "standalone.h" // standalone definitions
#include "proxmark3_arm.h"
#include "appmain.h"
#include "fpgaloader.h"
#include "lfops.h"
#include "util.h"
#include "dbprint.h"
#include "ticks.h"

#define OPTS 2

uint64_t low[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void ModInfo(void) {
    DbpString("  LF EM4xx standalone - aka SamyRun (Samy Kamkar)");
}

// samy's sniff and repeat routine for LF

//  LEDS.
//  A  ,  B  == which bank (recording)
//  FLASHING A, B =  clone bank
//  C = playing bank A
//  D = playing bank B

//J added below from lf_em410


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
//J added above from lf_em


void RunMod() {
    StandAloneMode();
    FpgaDownloadAndGo(FPGA_BITSTREAM_LF);
    Dbprintf(">>  LF EM41xx Read/Clone/Sim a.k.a modifiedSamyRun Started  <<");

    uint32_t high[OPTS], low[OPTS];
    int selected = 0;

#define STATE_READ 0
#define STATE_SIM 1
#define STATE_CLONE 2

    uint8_t state = STATE_READ;

    for (;;) {

        WDT_HIT();

        // exit from SamyRun,   send a usbcommand.
        if (data_available()) break;

        // Was our button held down or pressed?
        int button_pressed = BUTTON_HELD(280);
        if (button_pressed != BUTTON_HOLD)
            continue;
        /*
        #define BUTTON_NO_CLICK 0
        #define BUTTON_SINGLE_CLICK -1
        #define BUTTON_DOUBLE_CLICK -2
        */

        if (state == STATE_READ) {

            if (selected == 0) {
                LED_A_ON();
                LED_B_OFF();
            } else {
                LED_B_ON();
                LED_A_OFF();
            }

            LED_C_OFF();
            LED_D_OFF();

            WAIT_BUTTON_RELEASED();

            // record
            DbpString("[=] start recording");

            // findone, high, low, no ledcontrol (A)
            uint32_t hi = 0, lo = 0;
            CmdEM410xdemodFSK(1, &hi, &lo, 0);
            high[selected] = hi;
            low[selected] = lo;

            Dbprintf("[=]   recorded %x | %x%08x", selected, high[selected], low[selected]);

            // got nothing. blink and loop.
            if (hi == 0 && lo == 0) {
                SpinErr((selected == 0) ? LED_A : LED_B, 100, 12);
                DbpString("[=] only got zeros, retry recording after click");
                continue;
            }

            SpinErr((selected == 0) ? LED_A : LED_B, 250, 2);
            state = STATE_SIM;
            continue;

        } else if (state == STATE_SIM) {

            LED_C_ON();   // Simulate
            LED_D_OFF();
            WAIT_BUTTON_RELEASED();

            Dbprintf("[=] simulating %x | %x%08x", selected, high[selected], low[selected]);

            // high, low, no led control(A)  no time limit
            //J samyrun CmdHIDsimTAGEx(0, high[selected], low[selected], 0, false, -1);
            //J below added from lf_em410
            ConstructEM410xEmulBuf(ReversQuads(low[selected]));
            FlashLEDs(100, 5);
            SimulateTagLowFrequency(buflen, 0, 1);
            //J above added from lf_em410
            
            
            DbpString("[=] simulating done");

            uint8_t leds = ((selected == 0) ? LED_A : LED_B) | LED_C;
            SpinErr(leds, 250, 2);
            state = STATE_CLONE;
            continue;

        } else if (state == STATE_CLONE) {

            LED_C_OFF();
            LED_D_ON();   // clone
            WAIT_BUTTON_RELEASED();

            Dbprintf("[=]    cloning %x | %x%08x", selected, high[selected], low[selected]);

            // high2, high, low,  no longFMT
           //J samyrun CopyHIDtoT55x7(0, high[selected], low[selected], 0);
            
            //J
            WriteEM410x(0, high[selected], low[selected], 0);

            DbpString("[=] cloned done");

            state = STATE_READ;
            uint8_t leds = ((selected == 0) ? LED_A : LED_B) | LED_D;
            SpinErr(leds, 250, 2);
            selected = (selected + 1) % OPTS;
            LEDsoff();
        }
    }

    SpinErr((LED_A | LED_B | LED_C | LED_D), 250, 5);
    DbpString("[=] You can take shell back :) ...");
    LEDsoff();
}
