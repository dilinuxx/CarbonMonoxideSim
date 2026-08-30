/******************************************************************************
 *
 * ZE07-CO + MAX78000FTHR
 *
 * Reads CO concentration from the Winsen ZE07-CO using UART2.
 *
 * ZE07-CO UART:
 *   9600 baud
 *   8 data bits
 *   No parity
 *   1 stop bit
 *
 * Default ZE07-CO frame:
 *
 *   Byte 0: FF        Start
 *   Byte 1: 04        CO
 *   Byte 2: 03        ppm
 *   Byte 3: 01        Decimal place
 *   Byte 4: CO high
 *   Byte 5: CO low
 *   Byte 6: Range high
 *   Byte 7: Range low
 *   Byte 8: Checksum
 *
 ~/MaximSDK/Tools/OpenOCD/bin/openocd \
    -s ~/MaximSDK/Tools/OpenOCD/scripts \
    -f interface/cmsis-dap.cfg \
    -f target/max78000.cfg \
    -c "program /home/dilinox/MaximSDK/Examples/MAX78000/emon_UART/build/max78000.elf verify reset exit"

    screen /dev/ttyACM0 115200
    sudo lsof /dev/ttyACM0
    
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>

#include "mxc_device.h"
#include "board.h"
#include "uart.h"
#include "mxc_delay.h"
#include "led.h"

/***** Definitions *****/

#define UART_BAUD       9600
#define FRAME_LENGTH    9

#define ZE07_START_BYTE 0xFF
#define ZE07_CO_TYPE    0x04

/***** Globals *****/

static uint8_t rx_buffer[FRAME_LENGTH];

/***** Functions *****/

/*
 * Calculate ZE07-CO checksum.
 *
 * Checksum = ~(Byte1 + Byte2 + ... + Byte7) + 1
 */
static uint8_t ze07_checksum(const uint8_t *data)
{
    uint8_t sum = 0;

    for (int i = 1; i <= 7; i++) {
        sum += data[i];
    }

    return (uint8_t)(~sum + 1);
}


/*
 * Read one complete ZE07-CO frame.
 */
static int ze07_read_frame(void)
{
    uint8_t byte;
    int len;
    int ret;

    /*
     * Search for the 0xFF start byte.
     */
    while (1) {

        len = 1;

        ret = MXC_UART_Read(
            MXC_UART2,
            &byte,
            &len
        );

        if (ret != E_NO_ERROR) {
            return ret;
        }

        if (len != 1) {
            continue;
        }

        if (byte == ZE07_START_BYTE) {
            break;
        }
    }

    rx_buffer[0] = byte;

    /*
     * Read remaining 8 bytes.
     */
    len = FRAME_LENGTH - 1;

    ret = MXC_UART_Read(
        MXC_UART2,
        &rx_buffer[1],
        &len
    );

    if (ret != E_NO_ERROR) {
        return ret;
    }

    if (len != FRAME_LENGTH - 1) {
        return E_BAD_STATE;
    }

    /*
     * Verify that this is a CO frame.
     */
    if (rx_buffer[1] != ZE07_CO_TYPE) {
        return E_BAD_STATE;
    }

    /*
     * Verify checksum.
     */
    if (ze07_checksum(rx_buffer) != rx_buffer[8]) {
        return E_BAD_STATE;
    }

    return E_NO_ERROR;
}


/*****************************************************************************/

int main(void)
{
    int error;

    printf("\n");
    printf("********************************************\n");
    printf("* MAX78000FTHR + Winsen ZE07-CO            *\n");
    printf("* UART2 CO Sensor                          *\n");
    printf("********************************************\n\n");

    printf("UART2 configuration:\n");
    printf("  Baud     : %d\n", UART_BAUD);
    printf("  Format   : 8-N-1\n");
    printf("  Sensor   : ZE07-CO\n");
    printf("  Range    : 0-500 ppm\n\n");

    /*
     * Initialize UART2.
     */
    error = MXC_UART_Init(
        MXC_UART2,
        UART_BAUD,
        MXC_UART_APB_CLK
    );

    if (error != E_NO_ERROR) {

        printf(
            "ERROR: UART2 initialization failed: %d\n",
            error
        );

        while (1) {

            LED_On(LED_RED);
            MXC_Delay(MXC_DELAY_MSEC(250));

            LED_Off(LED_RED);
            MXC_Delay(MXC_DELAY_MSEC(250));
        }
    }

    printf("UART2 initialized successfully.\n");
    printf("Waiting for ZE07-CO data...\n\n");

    /*
     * Continuously receive sensor frames.
     */
    while (1) {

        error = ze07_read_frame();

        if (error == E_NO_ERROR) {

            /*
             * CO concentration is stored in bytes 4 and 5.
             *
             * The value is represented in 0.1 ppm units.
             */
            uint16_t concentration_x10;

            concentration_x10 =
                ((uint16_t)rx_buffer[4] << 8) |
                rx_buffer[5];

            printf(
                "CO: %u.%u ppm\n",
                concentration_x10 / 10,
                concentration_x10 % 10
            );

            /*
             * Valid frame received.
             */
            LED_On(LED_GREEN);
            MXC_Delay(MXC_DELAY_MSEC(20));
            LED_Off(LED_GREEN);

        } else {

            printf(
                "Invalid ZE07-CO frame: error %d\n",
                error
            );

            LED_On(LED_RED);
            MXC_Delay(MXC_DELAY_MSEC(20));
            LED_Off(LED_RED);
        }
    }
}
