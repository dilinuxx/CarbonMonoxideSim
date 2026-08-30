/******************************************************************************
 *
 * MAX78000FTHR + Winsen ZE07-CO + ESP32-H2
 *
 * DEBUG-SAFE VERSION
 *
 *
 * HOST_UART_ENABLED = 0
 *
 * means the ESP32-H2 UART is NOT USED for transmission.
 *
 * This allows the ZE07-CO sensor to be tested completely independently.
 *
 * Once the ZE07 is confirmed working:
 *
 *     #define HOST_UART_ENABLED 1
 *
 * to enable the MAX78000 -> ESP32-H2 protocol.
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
#include <stdbool.h>
#include <string.h>

#include "mxc_device.h"
#include "board.h"
#include "uart.h"
#include "mxc_delay.h"
#include "led.h"


/******************************************************************************
 * CONFIGURATION
 ******************************************************************************/

/*
 * Set to 0 while testing the ZE07 alone.
 *
 * Set to 1 once the ESP32-H2 is connected.
 */
#define HOST_UART_ENABLED       0


/******************************************************************************
 * SENSOR UART
 ******************************************************************************/

/*
 * ZE07-CO:
 *
 * ZE07 TX ---> MAX78000 P1_0 / UART2 RX
 * ZE07 RX <--- MAX78000 P1_1 / UART2 TX
 */
#define SENSOR_UART             MXC_UART2
#define SENSOR_UART_BAUD        9600


/******************************************************************************
 * HOST UART
 ******************************************************************************/

/*
 * ESP32-H2:
 *
 * MAX78000 P2_6 = UART3 RX
 * MAX78000 P2_7 = UART3 TX
 */
#define HOST_UART               MXC_UART3
#define HOST_UART_BAUD          115200


/******************************************************************************
 * TIMING
 ******************************************************************************/

#define STATUS_PERIOD_MS        10000
#define HEARTBEAT_PERIOD_MS     5000
#define SENSOR_TIMEOUT_MS       5000
#define PROCESS_PERIOD_MS       10


/******************************************************************************
 * HOST PROTOCOL
 ******************************************************************************/

#define PROTOCOL_START          0x7E
#define PROTOCOL_VERSION        0x01
#define PROTOCOL_MAX_PAYLOAD    32


#define MSG_HEARTBEAT           0x01
#define MSG_CO_STATUS           0x02
#define MSG_STATE_CHANGE        0x03
#define MSG_SENSOR_FAULT       0x04
#define MSG_DEVICE_INFO         0x05


/******************************************************************************
 * CO STATES
 ******************************************************************************/

typedef enum {

    CO_STATE_INIT = 0,
    CO_STATE_NORMAL,
    CO_STATE_WARNING,
    CO_STATE_ALARM,
    CO_STATE_SENSOR_FAULT

} co_state_t;


/******************************************************************************
 * SENSOR STATUS
 ******************************************************************************/

typedef enum {

    SENSOR_STATUS_OK = 0,
    SENSOR_STATUS_NO_DATA,
    SENSOR_STATUS_BAD_FRAME,
    SENSOR_STATUS_BAD_CHECKSUM,
    SENSOR_STATUS_TIMEOUT,
    SENSOR_STATUS_INVALID_VALUE

} sensor_status_t;


/******************************************************************************
 * CO THRESHOLDS
 *
 * Example values only.
 ******************************************************************************/

#define CO_WARNING_PPM         50
#define CO_ALARM_PPM           100


/******************************************************************************
 * ZE07-CO
 ******************************************************************************/

/*
 * ZE07 default frame:
 *
 * Byte 0 = FF
 * Byte 1 = 04
 * Byte 2 = 03
 * Byte 3 = 01
 * Byte 4 = CO high
 * Byte 5 = CO low
 * Byte 6 = range high
 * Byte 7 = range low
 * Byte 8 = checksum
 */

#define ZE07_FRAME_LENGTH       9
#define ZE07_START_BYTE         0xFF
#define ZE07_CO_TYPE            0x04
#define ZE07_MAX_PPM            500


static uint8_t ze07_rx_buffer[ZE07_FRAME_LENGTH];


/******************************************************************************
 * DEVICE INFORMATION
 ******************************************************************************/

#define DEVICE_ID               0x00000001UL

#define FIRMWARE_MAJOR          1
#define FIRMWARE_MINOR          0
#define FIRMWARE_PATCH          0


/******************************************************************************
 * APPLICATION STATE
 ******************************************************************************/

static uint16_t g_co_ppm_x10 = 0;

static co_state_t g_co_state =
    CO_STATE_INIT;

static sensor_status_t g_sensor_status =
    SENSOR_STATUS_NO_DATA;

static uint32_t g_last_sensor_ms = 0;

static uint32_t g_last_status_ms = 0;

static uint32_t g_last_heartbeat_ms = 0;

static uint16_t g_sequence = 0;

static uint32_t g_valid_frames = 0;

static uint32_t g_invalid_frames = 0;


/******************************************************************************
 * SYSTEM TIMEBASE
 ******************************************************************************/

static volatile uint32_t g_system_ms = 0;


/******************************************************************************
 * SysTick
 ******************************************************************************/

void SysTick_Handler(void)
{
    g_system_ms++;
}


/******************************************************************************
 * system_millis
 ******************************************************************************/

static uint32_t system_millis(void)
{
    return g_system_ms;
}


/******************************************************************************
 * Initialize system timebase
 ******************************************************************************/

static int system_time_init(void)
{
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) {

        return E_BAD_STATE;
    }

    return E_NO_ERROR;
}


/******************************************************************************
 * ZE07 CHECKSUM
 *
 * Checksum = ~(Byte1 + Byte2 + ... + Byte7) + 1
 ******************************************************************************/

static uint8_t ze07_checksum(
    const uint8_t *data
)
{
    uint8_t sum = 0;

    for (int i = 1; i <= 7; i++) {

        sum += data[i];
    }

    return (uint8_t)(~sum + 1);
}


/******************************************************************************
 * READ ZE07 FRAME
 *
 * This is intentionally the SAME blocking method used by the
 * known-working ZE07-only program.
 ******************************************************************************/

static int ze07_read_frame(void)
{
    uint8_t byte;

    int len;

    int ret;


    /**************************************************************************
     * Search for FF
     **************************************************************************/

    while (1) {

        len = 1;

        ret = MXC_UART_Read(
            SENSOR_UART,
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


    ze07_rx_buffer[0] = byte;


    /**************************************************************************
     * Read remaining 8 bytes
     **************************************************************************/

    len = ZE07_FRAME_LENGTH - 1;

    ret = MXC_UART_Read(
        SENSOR_UART,
        &ze07_rx_buffer[1],
        &len
    );

    if (ret != E_NO_ERROR) {

        return ret;
    }

    if (len != ZE07_FRAME_LENGTH - 1) {

        return E_BAD_STATE;
    }


    /**************************************************************************
     * Check sensor type
     **************************************************************************/

    if (ze07_rx_buffer[1] != ZE07_CO_TYPE) {

        return E_BAD_STATE;
    }


    /**************************************************************************
     * Check checksum
     **************************************************************************/

    if (ze07_checksum(ze07_rx_buffer) !=
        ze07_rx_buffer[8]) {

        return E_BAD_STATE;
    }


    return E_NO_ERROR;
}


/******************************************************************************
 * GET CO VALUE
 *
 * Returns value in 0.1 ppm units.
 *
 * Example:
 *
 *     5  -> 0.5 ppm
 *     25 -> 2.5 ppm
 *     123 -> 12.3 ppm
 ******************************************************************************/

static uint16_t ze07_get_ppm_x10(void)
{
    return ((uint16_t)ze07_rx_buffer[4] << 8) |
           ze07_rx_buffer[5];
}


/******************************************************************************
 * EVALUATE CO STATE
 ******************************************************************************/

static co_state_t co_evaluate_state(
    uint16_t ppm_x10
)
{
    uint16_t ppm =
        ppm_x10 / 10;


    if (ppm >= CO_ALARM_PPM) {

        return CO_STATE_ALARM;
    }


    if (ppm >= CO_WARNING_PPM) {

        return CO_STATE_WARNING;
    }


    return CO_STATE_NORMAL;
}


/******************************************************************************
 * CRC16-CCITT
 ******************************************************************************/

static uint16_t crc16_ccitt(
    const uint8_t *data,
    uint16_t length
)
{
    uint16_t crc = 0xFFFF;


    for (uint16_t i = 0; i < length; i++) {

        crc ^=
            ((uint16_t)data[i] << 8);


        for (uint8_t bit = 0; bit < 8; bit++) {

            if (crc & 0x8000) {

                crc =
                    (crc << 1) ^ 0x1021;

            } else {

                crc <<= 1;
            }
        }
    }


    return crc;
}


/******************************************************************************
 * HOST UART WRITE
 *
 * IMPORTANT:
 *
 * When HOST_UART_ENABLED == 0:
 *
 *     NOTHING IS SENT.
 *
 * This means the ESP32-H2 can be physically disconnected.
 ******************************************************************************/

static int host_uart_write(
    const uint8_t *data,
    uint16_t length
)
{
#if HOST_UART_ENABLED

    int len = (int)length;

    printf(
        "[HOST] Transmitting %u bytes...\n",
        length
    );

    int ret =
        MXC_UART_Write(
            HOST_UART,
            data,
            &len
        );

    printf(
        "[HOST] UART write returned %d\n",
        ret
    );

    return ret;

#else

    /*
     * Host disabled.
     *
     * Pretend transmission succeeded.
     */
    (void)data;
    (void)length;

    return E_NO_ERROR;

#endif
}


/******************************************************************************
 * BUILD AND SEND HOST MESSAGE
 ******************************************************************************/

static int host_send_message(
    uint8_t type,
    const uint8_t *payload,
    uint8_t payload_length
)
{
    uint8_t frame[64];

    uint16_t index = 0;

    uint16_t crc;


    if (payload_length >
        PROTOCOL_MAX_PAYLOAD) {

        return E_BAD_PARAM;
    }


    /**************************************************************************
     * START
     **************************************************************************/

    frame[index++] =
        PROTOCOL_START;


    /**************************************************************************
     * VERSION
     **************************************************************************/

    frame[index++] =
        PROTOCOL_VERSION;


    /**************************************************************************
     * TYPE
     **************************************************************************/

    frame[index++] =
        type;


    /**************************************************************************
     * LENGTH
     **************************************************************************/

    frame[index++] =
        payload_length;


    /**************************************************************************
     * PAYLOAD
     **************************************************************************/

    if (payload_length > 0 &&
        payload != NULL) {

        memcpy(
            &frame[index],
            payload,
            payload_length
        );

        index += payload_length;
    }


    /**************************************************************************
     * CRC
     *
     * CRC covers:
     *
     * VERSION
     * TYPE
     * LENGTH
     * PAYLOAD
     **************************************************************************/

    crc =
        crc16_ccitt(
            &frame[1],
            index - 1
        );


    frame[index++] =
        (uint8_t)(crc >> 8);

    frame[index++] =
        (uint8_t)(crc & 0xFF);


    return host_uart_write(
        frame,
        index
    );
}


/******************************************************************************
 * NEXT SEQUENCE
 ******************************************************************************/

static uint16_t next_sequence(void)
{
    g_sequence++;

    return g_sequence;
}


/******************************************************************************
 * SEND HEARTBEAT
 ******************************************************************************/

static void send_heartbeat(void)
{
    uint8_t payload[8];

    uint32_t device_id =
        DEVICE_ID;

    uint16_t sequence =
        next_sequence();


    payload[0] =
        (uint8_t)(device_id >> 24);

    payload[1] =
        (uint8_t)(device_id >> 16);

    payload[2] =
        (uint8_t)(device_id >> 8);

    payload[3] =
        (uint8_t)device_id;


    payload[4] =
        (uint8_t)(sequence >> 8);

    payload[5] =
        (uint8_t)sequence;


    payload[6] =
        (uint8_t)g_sensor_status;

    payload[7] =
        (uint8_t)g_co_state;


    host_send_message(
        MSG_HEARTBEAT,
        payload,
        sizeof(payload)
    );
}


/******************************************************************************
 * SEND CO STATUS
 ******************************************************************************/

static void send_co_status(void)
{
    uint8_t payload[14];

    uint32_t device_id =
        DEVICE_ID;

    uint32_t valid_frames =
        g_valid_frames;

    uint16_t sequence =
        next_sequence();


    payload[0] =
        (uint8_t)(device_id >> 24);

    payload[1] =
        (uint8_t)(device_id >> 16);

    payload[2] =
        (uint8_t)(device_id >> 8);

    payload[3] =
        (uint8_t)device_id;


    payload[4] =
        (uint8_t)(sequence >> 8);

    payload[5] =
        (uint8_t)sequence;


    payload[6] =
        (uint8_t)(g_co_ppm_x10 >> 8);

    payload[7] =
        (uint8_t)g_co_ppm_x10;


    payload[8] =
        (uint8_t)g_co_state;

    payload[9] =
        (uint8_t)g_sensor_status;


    payload[10] =
        (uint8_t)(valid_frames >> 24);

    payload[11] =
        (uint8_t)(valid_frames >> 16);

    payload[12] =
        (uint8_t)(valid_frames >> 8);

    payload[13] =
        (uint8_t)valid_frames;


    host_send_message(
        MSG_CO_STATUS,
        payload,
        sizeof(payload)
    );
}


/******************************************************************************
 * SEND STATE CHANGE
 ******************************************************************************/

static void send_state_change(
    co_state_t previous_state
)
{
    uint8_t payload[11];

    uint32_t device_id =
        DEVICE_ID;

    uint16_t sequence =
        next_sequence();


    payload[0] =
        (uint8_t)(device_id >> 24);

    payload[1] =
        (uint8_t)(device_id >> 16);

    payload[2] =
        (uint8_t)(device_id >> 8);

    payload[3] =
        (uint8_t)device_id;


    payload[4] =
        (uint8_t)(sequence >> 8);

    payload[5] =
        (uint8_t)sequence;


    payload[6] =
        (uint8_t)previous_state;

    payload[7] =
        (uint8_t)g_co_state;


    payload[8] =
        (uint8_t)(g_co_ppm_x10 >> 8);

    payload[9] =
        (uint8_t)g_co_ppm_x10;


    payload[10] =
        (uint8_t)g_sensor_status;


    host_send_message(
        MSG_STATE_CHANGE,
        payload,
        sizeof(payload)
    );
}


/******************************************************************************
 * SEND SENSOR FAULT
 ******************************************************************************/

static void send_sensor_fault(void)
{
    uint8_t payload[7];

    uint32_t device_id =
        DEVICE_ID;

    uint16_t sequence =
        next_sequence();


    payload[0] =
        (uint8_t)(device_id >> 24);

    payload[1] =
        (uint8_t)(device_id >> 16);

    payload[2] =
        (uint8_t)(device_id >> 8);

    payload[3] =
        (uint8_t)device_id;


    payload[4] =
        (uint8_t)(sequence >> 8);

    payload[5] =
        (uint8_t)sequence;


    payload[6] =
        (uint8_t)g_sensor_status;


    host_send_message(
        MSG_SENSOR_FAULT,
        payload,
        sizeof(payload)
    );
}


/******************************************************************************
 * SEND DEVICE INFO
 ******************************************************************************/

static void send_device_info(void)
{
    uint8_t payload[10];

    uint32_t device_id =
        DEVICE_ID;


    /*
     * Device ID.
     */
    payload[0] =
        (uint8_t)(device_id >> 24);

    payload[1] =
        (uint8_t)(device_id >> 16);

    payload[2] =
        (uint8_t)(device_id >> 8);

    payload[3] =
        (uint8_t)device_id;


    /*
     * Firmware.
     */
    payload[4] =
        FIRMWARE_MAJOR;

    payload[5] =
        FIRMWARE_MINOR;

    payload[6] =
        FIRMWARE_PATCH;


    /*
     * Sensor type.
     *
     * 0x01 = ZE07-CO
     */
    payload[7] =
        0x01;


    /*
     * Hardware.
     *
     * 0x01 = MAX78000FTHR
     */
    payload[8] =
        0x01;


    /*
     * Protocol version.
     */
    payload[9] =
        PROTOCOL_VERSION;


    host_send_message(
        MSG_DEVICE_INFO,
        payload,
        sizeof(payload)
    );
}


/******************************************************************************
 * SENSOR UART INITIALIZATION
 ******************************************************************************/

static int sensor_uart_init(void)
{
    return MXC_UART_Init(
        SENSOR_UART,
        SENSOR_UART_BAUD,
        MXC_UART_APB_CLK
    );
}


/******************************************************************************
 * HOST UART INITIALIZATION
 ******************************************************************************/

static int host_uart_init(void)
{
#if HOST_UART_ENABLED

    return MXC_UART_Init(
        HOST_UART,
        HOST_UART_BAUD,
        MXC_UART_ERTCO_CLK
    );

#else

    /*
     * Host disabled.
     *
     * Do NOT initialize UART3.
     *
     * This completely removes UART3 from the test.
     */
    return E_NO_ERROR;

#endif
}


/******************************************************************************
 * PROCESS SENSOR
 ******************************************************************************/

static void process_sensor(void)
{
    int result;


    /*
     * Read exactly one complete ZE07 frame.
     *
     * This is intentionally identical in principle to the
     * known-working ZE07-only program.
     */
    result =
        ze07_read_frame();


    /**************************************************************************
     * Invalid frame
     **************************************************************************/

    if (result != E_NO_ERROR) {

        g_invalid_frames++;

        g_sensor_status =
            SENSOR_STATUS_BAD_FRAME;


        printf(
            "Invalid ZE07-CO frame: error %d\n",
            result
        );


        return;
    }


    /**************************************************************************
     * Extract CO
     **************************************************************************/

    g_co_ppm_x10 =
        ze07_get_ppm_x10();


    /**************************************************************************
     * Validate range
     **************************************************************************/

    if (g_co_ppm_x10 >
        (ZE07_MAX_PPM * 10)) {

        g_sensor_status =
            SENSOR_STATUS_INVALID_VALUE;

        g_invalid_frames++;


        printf(
            "Invalid CO value: %u.%u ppm\n",
            g_co_ppm_x10 / 10,
            g_co_ppm_x10 % 10
        );


        return;
    }


    /**************************************************************************
     * Sensor healthy
     **************************************************************************/

    g_sensor_status =
        SENSOR_STATUS_OK;

    g_valid_frames++;


    g_last_sensor_ms =
        system_millis();


    /**************************************************************************
     * Evaluate state
     **************************************************************************/

    co_state_t previous_state =
        g_co_state;


    g_co_state =
        co_evaluate_state(
            g_co_ppm_x10
        );


    /**************************************************************************
     * PRINT SENSOR VALUE
     **************************************************************************/

    printf(
        "CO: %u.%u ppm | state=%d | sensor=%d\n",
        g_co_ppm_x10 / 10,
        g_co_ppm_x10 % 10,
        g_co_state,
        g_sensor_status
    );


    /**************************************************************************
     * LED
     **************************************************************************/

    LED_On(LED_GREEN);


    /**************************************************************************
     * State transition
     **************************************************************************/

    if (g_co_state != previous_state) {

        send_state_change(
            previous_state
        );


        if (g_co_state ==
                CO_STATE_WARNING ||
            g_co_state ==
                CO_STATE_ALARM) {

            LED_On(LED_RED);

        } else {

            LED_Off(LED_RED);
        }
    }
}


/******************************************************************************
 * SENSOR HEALTH
 ******************************************************************************/

static void process_sensor_health(void)
{
    uint32_t now =
        system_millis();


    /*
     * No valid frames yet.
     */
    if (g_valid_frames == 0) {

        if (now >= SENSOR_TIMEOUT_MS) {

            if (g_sensor_status !=
                SENSOR_STATUS_TIMEOUT) {

                g_sensor_status =
                    SENSOR_STATUS_TIMEOUT;


                g_co_state =
                    CO_STATE_SENSOR_FAULT;


                LED_Off(LED_GREEN);

                LED_On(LED_RED);


                send_sensor_fault();


                printf(
                    "CO SENSOR TIMEOUT\n"
                );
            }
        }

        return;
    }


    /*
     * Sensor was working but stopped.
     */
    if ((now - g_last_sensor_ms) >
        SENSOR_TIMEOUT_MS) {

        if (g_sensor_status !=
            SENSOR_STATUS_TIMEOUT) {

            g_sensor_status =
                SENSOR_STATUS_TIMEOUT;


            g_co_state =
                CO_STATE_SENSOR_FAULT;


            LED_Off(LED_GREEN);

            LED_On(LED_RED);


            send_sensor_fault();


            printf(
                "CO SENSOR TIMEOUT\n"
            );
        }
    }
}


/******************************************************************************
 * PERIODIC COMMUNICATIONS
 ******************************************************************************/

static void process_communications(void)
{
    uint32_t now =
        system_millis();


    /*
     * CO status.
     */
    if ((now - g_last_status_ms) >=
        STATUS_PERIOD_MS) {

        g_last_status_ms =
            now;


        send_co_status();
    }


    /*
     * Heartbeat.
     */
    if ((now - g_last_heartbeat_ms) >=
        HEARTBEAT_PERIOD_MS) {

        g_last_heartbeat_ms =
            now;


        send_heartbeat();
    }
}


/******************************************************************************
 * MAIN
 ******************************************************************************/

int main(void)
{
    int error;


    /**************************************************************************
     * Banner
     **************************************************************************/

    printf("\n");

    printf(
        "=================================================\n"
    );

    printf(
        " MAX78000FTHR CO MONITOR\n"
    );

    printf(
        " ZE07-CO -> MAX78000 -> ESP32-H2\n"
    );

    printf(
        "=================================================\n\n"
    );


    /**************************************************************************
     * Sensor information
     **************************************************************************/

    printf(
        "Sensor UART:\n"
    );

    printf(
        "  Peripheral : UART2\n"
    );

    printf(
        "  RX         : P1_0\n"
    );

    printf(
        "  TX         : P1_1\n"
    );

    printf(
        "  Baud       : %d\n",
        SENSOR_UART_BAUD
    );

    printf(
        "  Format     : 8-N-1\n"
    );

    printf(
        "  Sensor     : ZE07-CO\n\n"
    );


    /**************************************************************************
     * Host information
     **************************************************************************/

    printf(
        "ESP32-H2 UART:\n"
    );

    printf(
        "  Peripheral : UART3 / LPUART0\n"
    );

    printf(
        "  RX         : P2_6\n"
    );

    printf(
        "  TX         : P2_7\n"
    );

    printf(
        "  Baud       : %d\n",
        HOST_UART_BAUD
    );

    printf(
        "  Host UART  : %s\n\n",
        HOST_UART_ENABLED ? "ENABLED" : "DISABLED"
    );


    /**************************************************************************
     * System time
     **************************************************************************/

    printf(
        "Initializing system timebase...\n"
    );

    error =
        system_time_init();


    if (error != E_NO_ERROR) {

        printf(
            "ERROR: System timebase initialization failed: %d\n",
            error
        );


        while (1) {

            LED_On(LED_RED);

            MXC_Delay(
                MXC_DELAY_MSEC(250)
            );

            LED_Off(LED_RED);

            MXC_Delay(
                MXC_DELAY_MSEC(250)
            );
        }
    }


    printf(
        "System timebase initialized successfully.\n"
    );


    /**************************************************************************
     * Sensor UART
     **************************************************************************/

    printf(
        "Initializing sensor UART2...\n"
    );

    error =
        sensor_uart_init();


    if (error != E_NO_ERROR) {

        printf(
            "ERROR: Sensor UART initialization failed: %d\n",
            error
        );


        while (1) {

            LED_On(LED_RED);

            MXC_Delay(
                MXC_DELAY_MSEC(250)
            );

            LED_Off(LED_RED);

            MXC_Delay(
                MXC_DELAY_MSEC(250)
            );
        }
    }


    printf(
        "Sensor UART2 initialized successfully.\n"
    );


    /**************************************************************************
     * Host UART
     **************************************************************************/

#if HOST_UART_ENABLED

    printf(
        "Initializing ESP32-H2 UART3...\n"
    );

#else

    printf(
        "ESP32-H2 UART3 DISABLED for this test.\n"
    );

#endif


    error =
        host_uart_init();


    if (error != E_NO_ERROR) {

        printf(
            "ERROR: ESP32-H2 UART3 initialization failed: %d\n",
            error
        );


        while (1) {

            LED_On(LED_RED);

            MXC_Delay(
                MXC_DELAY_MSEC(100)
            );

            LED_Off(LED_RED);

            MXC_Delay(
                MXC_DELAY_MSEC(100)
            );
        }
    }


#if HOST_UART_ENABLED

    printf(
        "ESP32-H2 UART3 initialized successfully.\n"
    );

#else

    printf(
        "ESP32-H2 UART3 is disabled.\n"
    );

#endif


    /**************************************************************************
     * Initial application state
     **************************************************************************/

    g_co_state =
        CO_STATE_INIT;

    g_sensor_status =
        SENSOR_STATUS_NO_DATA;


    /**************************************************************************
     * Clear sensor FIFO
     **************************************************************************/

    printf(
        "Clearing ZE07 UART2 RX FIFO...\n"
    );

    MXC_UART_ClearRXFIFO(
        SENSOR_UART
    );


    printf(
        "Sensor FIFO cleared.\n"
    );


    /**************************************************************************
     * Host initialization
     **************************************************************************/

#if HOST_UART_ENABLED

    printf(
        "Sending device information to ESP32-H2...\n"
    );

    error =
        host_send_message(
            MSG_DEVICE_INFO,
            NULL,
            0
        );

    /*
     * We intentionally don't use this simplified message above.
     *
     * Send the real device info next.
     */
    (void)error;


    send_device_info();

    printf(
        "Device information sent.\n"
    );


    printf(
        "Sending initial heartbeat...\n"
    );

    send_heartbeat();

    printf(
        "Initial heartbeat sent.\n"
    );

#else

    printf(
        "Skipping ESP32-H2 startup messages because HOST_UART_ENABLED=0.\n"
    );

#endif


    /**************************************************************************
     * READY
     **************************************************************************/

    printf("\n");

    printf(
        "=================================================\n"
    );

    printf(
        " SYSTEM READY\n"
    );

    printf(
        "=================================================\n"
    );

    printf(
        "Waiting for ZE07-CO frames...\n\n"
    );


    /**************************************************************************
     * MAIN LOOP
     **************************************************************************/

    while (1) {

        /*
         * Read ZE07.
         *
         * This blocks until a complete frame arrives,
         * exactly like the known-good sensor program.
         */
        process_sensor();


        /*
         * Check sensor health.
         */
        process_sensor_health();


        /*
         * Host communications.
         *
         * Disabled completely when HOST_UART_ENABLED=0.
         */
        process_communications();


        /*
         * Small delay.
         */
        MXC_Delay(
            MXC_DELAY_MSEC(PROCESS_PERIOD_MS)
        );
    }


    return 0;
}
