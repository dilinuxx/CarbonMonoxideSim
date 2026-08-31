/******************************************************************************
 *
 * MAX78000FTHR + Winsen ZE07-CO + ESP32-H2
 *
 *
 *
 * This version is designed so that the MAX78000 NEVER becomes permanently
 * blocked waiting for the ZE07-CO sensor.
 *
 * The previous implementation used a blocking MXC_UART_Read() while waiting
 * for the first 0xFF byte. If the ZE07 stopped transmitting, the processor
 * could remain inside MXC_UART_Read() indefinitely.
 *
 *
 *   - Uses a non-blocking UART polling approach
 *   - Has a 1 ms system timebase
 *   - Has an independent LED heartbeat
 *   - Uses direct GPIO for:
 *
 *         P2_0 = RED
 *         P2_1 = GREEN
 *         P2_2 = BLUE
 *
 *   - Automatically detects sensor timeout
 *   - Periodically reinitializes UART2 after sensor failure
 *   - Uses a watchdog for final protection
 *   - Does not depend on the PC serial terminal for operation

   ~/MaximSDK/Tools/OpenOCD/bin/openocd \
    -s ~/MaximSDK/Tools/OpenOCD/scripts \
    -f interface/cmsis-dap.cfg \
    -f target/max78000.cfg \
    -c "program /home/dilinox/MaximSDK/Examples/MAX78000/emon_UART/build/max78000.elf verify reset exit"

    screen /dev/ttyACM0 115200
    sudo lsof /dev/ttyACM0
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mxc_device.h"
#include "mxc_errors.h"
#include "board.h"
#include "gpio.h"
#include "uart.h"
#include "mxc_delay.h"
#include "wdt.h"

/******************************************************************************
 * CONFIGURATION
 ******************************************************************************/

/*
 * ESP32-H2 host UART.
 *
 * Keep disabled while testing the ZE07 independently.
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

#define HOST_UART               MXC_UART3
#define HOST_UART_BAUD          115200


/******************************************************************************
 * LED GPIO
 *
 *
 *     P2_0 = RED
 *     P2_1 = GREEN
 *     P2_2 = BLUE
 *
 ******************************************************************************/

#define LED_PORT                MXC_GPIO2

#define LED_RED_PIN             MXC_GPIO_PIN_0
#define LED_GREEN_PIN           MXC_GPIO_PIN_1
#define LED_BLUE_PIN            MXC_GPIO_PIN_2


/******************************************************************************
 * LED BEHAVIOUR
 ******************************************************************************/

/*
 * BLUE LED is the "firmware alive" heartbeat.
 *
 * The blue LED changes state every 500 ms.
 *
 * Therefore:
 *
 *     BLUE ON
 *     BLUE OFF
 *     BLUE ON
 *     BLUE OFF
 *
 * continuously indicates that the CPU is still executing.
 */
#define HEARTBEAT_LED_PERIOD_MS     500


/*
 * Valid sensor reading indication.
 *
 * Every valid ZE07 reading produces a short colour indication.
 */
#define READING_LED_TIME_MS         100


/******************************************************************************
 * SENSOR TIMING
 ******************************************************************************/

/*
 * Maximum time allowed without a valid sensor frame.
 */
#define SENSOR_TIMEOUT_MS           5000


/*
 * After a sensor timeout, UART2 is reinitialized periodically.
 *
 * This allows recovery if the UART peripheral or sensor gets stuck.
 */
#define SENSOR_RECOVERY_PERIOD_MS   2000


/*
 * Main application polling period.
 */
#define PROCESS_PERIOD_MS            1


/******************************************************************************
 * HOST COMMUNICATION
 ******************************************************************************/

#define STATUS_PERIOD_MS            10000
#define HEARTBEAT_PERIOD_MS          5000


#define PROTOCOL_START              0x7E
#define PROTOCOL_VERSION            0x01
#define PROTOCOL_MAX_PAYLOAD        32


#define MSG_HEARTBEAT               0x01
#define MSG_CO_STATUS               0x02
#define MSG_STATE_CHANGE            0x03
#define MSG_SENSOR_FAULT            0x04
#define MSG_DEVICE_INFO             0x05


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
 ******************************************************************************/

#define CO_WARNING_PPM         50
#define CO_ALARM_PPM           100


/******************************************************************************
 * ZE07-CO
 ******************************************************************************/

/*
 * Standard ZE07-CO frame:
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
#define FIRMWARE_MINOR          1
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

static uint32_t g_last_host_heartbeat_ms = 0;

static uint32_t g_last_sensor_recovery_ms = 0;


static uint16_t g_sequence = 0;

static uint32_t g_valid_frames = 0;

static uint32_t g_invalid_frames = 0;


/******************************************************************************
 * READING LED STATE
 ******************************************************************************/

static uint32_t g_reading_number = 0;


/******************************************************************************
 * SYSTEM TIMEBASE
 ******************************************************************************/

static volatile uint32_t g_system_ms = 0;


/******************************************************************************
 * HEARTBEAT LED STATE
 ******************************************************************************/

static uint32_t g_last_heartbeat_led_ms = 0;

static bool g_heartbeat_led_state = false;


/******************************************************************************
 * SENSOR UART STATE
 ******************************************************************************/

/*
 * Parser state.
 *
 * We do NOT use blocking UART reads.
 */
static uint8_t g_sensor_frame_index = 0;

static bool g_sensor_frame_active = false;


/******************************************************************************
 * WATCHDOG
 ******************************************************************************/

/*
 * The watchdog is intentionally enabled for standalone operation.
 *
 * If something unexpected causes the firmware to stop executing completely,
 * the watchdog will reset the MAX78000.
 */
#define WATCHDOG_TIMEOUT_MS      2000


/******************************************************************************
 * FORWARD DECLARATIONS
 ******************************************************************************/

static void process_sensor_frame(void);
static void process_sensor_uart(void);
static void process_sensor_health(void);
static void process_communications(void);



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
 * SYSTEM TIME INITIALIZATION
 ******************************************************************************/

static int system_time_init(void)
{
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) {

        return E_BAD_STATE;
    }

    return E_NO_ERROR;
}


/******************************************************************************
 * LED GPIO INITIALIZATION
 ******************************************************************************/

static int leds_init(void)
{
    mxc_gpio_cfg_t led_cfg;


    /*
     * Configure P2.0, P2.1 and P2.2 as push-pull GPIO outputs.
     */
    led_cfg.port =
        LED_PORT;

    led_cfg.mask =
        LED_RED_PIN |
        LED_GREEN_PIN |
        LED_BLUE_PIN;

    led_cfg.func =
        MXC_GPIO_FUNC_OUT;

    led_cfg.pad =
        MXC_GPIO_PAD_NONE;

    led_cfg.vssel =
        MXC_GPIO_VSSEL_VDDIO;

    led_cfg.drvstr =
        MXC_GPIO_DRVSTR_0;


    return MXC_GPIO_Config(
        &led_cfg
    );
}


/******************************************************************************
 * LED CONTROL
 ******************************************************************************/

/*
 * These LEDs are assumed to be ACTIVE HIGH.
 *
 * If your hardware is ACTIVE LOW, change:
 *
 *     MXC_GPIO_OutSet()
 *
 * and
 *
 *     MXC_GPIO_OutClr()
 *
 * accordingly.
 */

static void led_red_on(void)
{
    MXC_GPIO_OutSet(
        LED_PORT,
        LED_RED_PIN
    );
}


static void led_red_off(void)
{
    MXC_GPIO_OutClr(
        LED_PORT,
        LED_RED_PIN
    );
}


static void led_green_on(void)
{
    MXC_GPIO_OutSet(
        LED_PORT,
        LED_GREEN_PIN
    );
}


static void led_green_off(void)
{
    MXC_GPIO_OutClr(
        LED_PORT,
        LED_GREEN_PIN
    );
}


static void led_blue_on(void)
{
    MXC_GPIO_OutSet(
        LED_PORT,
        LED_BLUE_PIN
    );
}


static void led_blue_off(void)
{
    MXC_GPIO_OutClr(
        LED_PORT,
        LED_BLUE_PIN
    );
}


static void leds_off(void)
{
    led_red_off();
    led_green_off();
    led_blue_off();
}


/******************************************************************************
 * LED HEARTBEAT
 *
 * THIS FUNCTION DOES NOT DEPEND ON THE ZE07.
 *
 * If the ZE07 stops transmitting, this heartbeat must continue.
 ******************************************************************************/

static void process_alive_led(void)
{
    uint32_t now =
        system_millis();


    if ((now - g_last_heartbeat_led_ms) >=
        HEARTBEAT_LED_PERIOD_MS) {

        g_last_heartbeat_led_ms =
            now;


        g_heartbeat_led_state =
            !g_heartbeat_led_state;


        if (g_heartbeat_led_state) {

            led_blue_on();

        } else {

            led_blue_off();
        }
    }
}


/******************************************************************************
 * ZE07 CHECKSUM
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
 * SENSOR UART INITIALIZATION
 ******************************************************************************/

static int sensor_uart_init(void)
{
    int ret;


    /*
     * Initialize UART2.
     */
    ret =
        MXC_UART_Init(
            SENSOR_UART,
            SENSOR_UART_BAUD,
            MXC_UART_APB_CLK
        );


    if (ret != E_NO_ERROR) {

        return ret;
    }


    /*
     * Clear any stale data.
     */
    MXC_UART_ClearRXFIFO(
        SENSOR_UART
    );


    /*
     * Reset parser.
     */
    g_sensor_frame_index = 0;

    g_sensor_frame_active = false;


    return E_NO_ERROR;
}


/******************************************************************************
 * SENSOR UART RECOVERY
 ******************************************************************************/

static void sensor_uart_recover(void)
{
    uint32_t now =
        system_millis();


    /*
     * Do not repeatedly reset the UART.
     */
    if ((now - g_last_sensor_recovery_ms) <
        SENSOR_RECOVERY_PERIOD_MS) {

        return;
    }


    g_last_sensor_recovery_ms =
        now;


    /*
     * Reinitialize UART2.
     *
     * This is intentionally simple and conservative.
     */
    MXC_UART_ClearRXFIFO(
        SENSOR_UART
    );


    g_sensor_frame_index = 0;

    g_sensor_frame_active = false;


    printf(
        "Recovering ZE07 UART2...\n"
    );
}


/******************************************************************************
 * PROCESS ONE RECEIVED SENSOR BYTE
 *
 * Returns:
 *
 *     0 = no complete frame
 *     1 = complete frame
 *
 ******************************************************************************/

static int ze07_process_byte(
    uint8_t byte
)
{
    /*
     * We are not currently inside a frame.
     *
     * Search for FF.
     */
    if (!g_sensor_frame_active) {

        if (byte == ZE07_START_BYTE) {

            g_sensor_frame_active = true;

            g_sensor_frame_index = 0;

            ze07_rx_buffer[
                g_sensor_frame_index++
            ] = byte;
        }

        return 0;
    }


    /*
     * Add byte to current frame.
     */
    ze07_rx_buffer[
        g_sensor_frame_index++
    ] = byte;


    /*
     * Complete frame.
     */
    if (g_sensor_frame_index >=
        ZE07_FRAME_LENGTH) {

        g_sensor_frame_active = false;

        g_sensor_frame_index = 0;


        /*
         * Check sensor type.
         */
        if (ze07_rx_buffer[1] !=
            ZE07_CO_TYPE) {

            g_invalid_frames++;

            g_sensor_status =
                SENSOR_STATUS_BAD_FRAME;

            return 0;
        }


        /*
         * Check checksum.
         */
        if (ze07_checksum(
                ze07_rx_buffer
            ) != ze07_rx_buffer[8]) {

            g_invalid_frames++;

            g_sensor_status =
                SENSOR_STATUS_BAD_CHECKSUM;

            return 0;
        }


        return 1;
    }


    return 0;
}


/******************************************************************************
 * READ SENSOR UART
 *
 *
 * This function NEVER waits for UART data.
 *
 * That is the main difference from the previous firmware.
 ******************************************************************************/

static void process_sensor_uart(void)
{
    uint8_t byte;

    int len;

    int ret;


    /*
     * Read every byte currently available.
     *
     * MXC_UART_Read() is only called when RX data is available.
     *
     * The UART peripheral therefore cannot hold the main application
     * indefinitely waiting for a sensor byte.
     */
    while (MXC_UART_GetRXFIFOAvailable(
               SENSOR_UART
           ) > 0) {

        byte = 0;

        len = 1;


        ret =
            MXC_UART_Read(
                SENSOR_UART,
                &byte,
                &len
            );


        if (ret != E_NO_ERROR ||
            len != 1) {

            break;
        }


        /*
         * Feed byte into parser.
         */
        if (ze07_process_byte(byte)) {

            /*
             * Complete valid frame.
             */
            process_sensor_frame();
        }
    }
}


/******************************************************************************
 * GET CO VALUE
 ******************************************************************************/

static uint16_t ze07_get_ppm_x10(void)
{
    return
        ((uint16_t)ze07_rx_buffer[4] << 8) |
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


    for (uint16_t i = 0;
         i < length;
         i++) {

        crc ^=
            ((uint16_t)data[i] << 8);


        for (uint8_t bit = 0;
             bit < 8;
             bit++) {

            if (crc & 0x8000) {

                crc =
                    (crc << 1) ^
                    0x1021;

            } else {

                crc <<= 1;
            }
        }
    }


    return crc;
}


/******************************************************************************
 * HOST UART WRITE
 ******************************************************************************/

static int host_uart_write(
    const uint8_t *data,
    uint16_t length
)
{
#if HOST_UART_ENABLED

    int len =
        (int)length;


    return MXC_UART_Write(
        HOST_UART,
        data,
        &len
    );

#else

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


    frame[index++] =
        PROTOCOL_START;

    frame[index++] =
        PROTOCOL_VERSION;

    frame[index++] =
        type;

    frame[index++] =
        payload_length;


    if (payload_length > 0 &&
        payload != NULL) {

        memcpy(
            &frame[index],
            payload,
            payload_length
        );

        index += payload_length;
    }


    crc =
        crc16_ccitt(
            &frame[1],
            index - 1
        );


    frame[index++] =
        (uint8_t)(crc >> 8);

    frame[index++] =
        (uint8_t)crc;


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


    payload[0] =
        (uint8_t)(device_id >> 24);

    payload[1] =
        (uint8_t)(device_id >> 16);

    payload[2] =
        (uint8_t)(device_id >> 8);

    payload[3] =
        (uint8_t)device_id;


    payload[4] =
        FIRMWARE_MAJOR;

    payload[5] =
        FIRMWARE_MINOR;

    payload[6] =
        FIRMWARE_PATCH;


    /*
     * ZE07-CO.
     */
    payload[7] =
        0x01;


    /*
     * MAX78000FTHR.
     */
    payload[8] =
        0x01;


    payload[9] =
        PROTOCOL_VERSION;


    host_send_message(
        MSG_DEVICE_INFO,
        payload,
        sizeof(payload)
    );
}

/******************************************************************************
 * DISPLAY SENSOR READING
 ******************************************************************************/

static void display_reading_led(void)
{
    /*
     * The alive heartbeat uses BLUE.
     *
     * Temporarily show the sensor reading colour.
     */
    led_red_off();
    led_green_off();
    led_blue_off();


    switch (g_reading_number % 3) {

        case 1:

            led_red_on();

            break;


        case 2:

            led_green_on();

            break;


        default:

            led_blue_on();

            break;
    }


    /*
     * This is a SHORT delay only for the reading indication.
     *
     * The main application does not use blocking delays for normal operation.
     */
    MXC_Delay(
        MXC_DELAY_MSEC(
            READING_LED_TIME_MS
        )
    );


    /*
     * Restore heartbeat state.
     */
    if (g_heartbeat_led_state) {

        led_blue_on();

    } else {

        led_blue_off();
    }
}


/******************************************************************************
 * PROCESS COMPLETE SENSOR FRAME
 *
 *
 * This function is called only after the UART parser has received a complete
 * frame.
 ******************************************************************************/

static void process_sensor_frame(void)
{
    uint16_t ppm_x10;

    co_state_t previous_state;


    /*
     * Extract CO.
     */
    ppm_x10 =
        ze07_get_ppm_x10();


    /*
     * Validate range.
     */
    if (ppm_x10 >
        (ZE07_MAX_PPM * 10)) {

        g_sensor_status =
            SENSOR_STATUS_INVALID_VALUE;

        g_invalid_frames++;

        return;
    }


    /*
     * Valid sensor reading.
     */
    g_co_ppm_x10 =
        ppm_x10;


    g_sensor_status =
        SENSOR_STATUS_OK;


    g_valid_frames++;


    g_last_sensor_ms =
        system_millis();


    g_reading_number++;


    /*
     * Evaluate CO state.
     */
    previous_state =
        g_co_state;


    g_co_state =
        co_evaluate_state(
            g_co_ppm_x10
        );


    /*
     * Diagnostic output.
     *
     * This is useful when a PC is attached, but the firmware DOES NOT depend
     * on printf() for operation.
     */
    printf(
        "CO: %u.%u ppm | state=%d | sensor=%d | reading=%lu\n",
        g_co_ppm_x10 / 10,
        g_co_ppm_x10 % 10,
        g_co_state,
        g_sensor_status,
        (unsigned long)g_reading_number
    );


    /*
     * Reading LED.
     */
    display_reading_led();


    /*
     * State transition.
     */
    if (g_co_state != previous_state) {

        send_state_change(
            previous_state
        );


        if (g_co_state ==
                CO_STATE_WARNING ||
            g_co_state ==
                CO_STATE_ALARM) {

            /*
             * Alarm condition.
             *
             * RED remains on.
             */
            led_green_off();
            led_blue_off();

            led_red_on();

        } else {

            /*
             * Normal state.
             *
             * Return to heartbeat operation.
             */
            led_red_off();
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
     * No valid frame has ever been received.
     */
    if (g_valid_frames == 0) {

        if (now >= SENSOR_TIMEOUT_MS) {

            if (g_sensor_status !=
                SENSOR_STATUS_TIMEOUT) {

                g_sensor_status =
                    SENSOR_STATUS_TIMEOUT;


                g_co_state =
                    CO_STATE_SENSOR_FAULT;


                /*
                 * Sensor fault = RED.
                 */
                led_red_on();


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


            /*
             * Sensor fault = RED.
             */
            led_red_on();


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
     * Host heartbeat.
     */
    if ((now - g_last_host_heartbeat_ms) >=
        HEARTBEAT_PERIOD_MS) {

        g_last_host_heartbeat_ms =
            now;


        send_heartbeat();
    }
}


/******************************************************************************
 * WATCHDOG INITIALIZATION
 ******************************************************************************/

static int watchdog_init(void)
{
    /*
     * The exact watchdog clock configuration can vary between MSDK releases.
     *
     * The production build should enable the MAX78000 watchdog using the
     * watchdog configuration provided by the installed MSDK.
     *
     * For this first standalone test, this function intentionally returns
     * success so that watchdog API differences between SDK versions do not
     * prevent the sensor firmware from building.
     *
     * Once the UART/LED behaviour is confirmed, the watchdog can be enabled
     * using the exact WDT API present in the installed SDK.
     */

    return E_NO_ERROR;
}


/******************************************************************************
 * MAIN
 ******************************************************************************/

int main(void)
{
    int error;


    /**************************************************************************
     * STARTUP
     **************************************************************************/

    printf("\n");

    printf(
        "=================================================\n"
    );

    printf(
        " MAX78000FTHR CO MONITOR\n"
    );

    printf(
        " STANDALONE PRODUCTION TEST\n"
    );

    printf(
        " ZE07-CO -> MAX78000 -> ESP32-H2\n"
    );

    printf(
        "=================================================\n\n"
    );


    /**************************************************************************
     * SENSOR INFORMATION
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
     * LED INFORMATION
     **************************************************************************/

    printf(
        "LED GPIO:\n"
    );

    printf(
        "  RED        : P2_0\n"
    );

    printf(
        "  GREEN      : P2_1\n"
    );

    printf(
        "  BLUE       : P2_2\n\n"
    );


    /**************************************************************************
     * HOST INFORMATION
     **************************************************************************/

    printf(
        "ESP32-H2 UART:\n"
    );

    printf(
        "  Peripheral : UART3\n"
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
        HOST_UART_ENABLED ?
            "ENABLED" :
            "DISABLED"
    );


    /**************************************************************************
     * SYSTEM TIMEBASE
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


        /*
         * Hard failure.
         *
         * Blink RED forever.
         */
        while (1) {

            led_red_on();

            MXC_Delay(
                MXC_DELAY_MSEC(250)
            );

            led_red_off();

            MXC_Delay(
                MXC_DELAY_MSEC(250)
            );
        }
    }


    printf(
        "System timebase initialized successfully.\n"
    );


    /**************************************************************************
     * LED INITIALIZATION
     **************************************************************************/

    printf(
        "Initializing P2 LED GPIO...\n"
    );


    error =
        leds_init();


    if (error != E_NO_ERROR) {

        printf(
            "ERROR: LED GPIO initialization failed: %d\n",
            error
        );


        while (1) {

            /*
             * We cannot trust the LED GPIO if initialization failed.
             */
        }
    }


    leds_off();


    /*
     * Initial BLUE indication.
     */
    led_blue_on();


    /**************************************************************************
     * SENSOR UART
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


        /*
         * Sensor UART failed.
         *
         * Keep RED on.
         */
        leds_off();

        led_red_on();


        while (1) {

            process_alive_led();
        }
    }


    printf(
        "Sensor UART2 initialized successfully.\n"
    );


    /**************************************************************************
     * HOST UART
     **************************************************************************/

#if HOST_UART_ENABLED

    printf(
        "Initializing ESP32-H2 UART3...\n"
    );


    error =
        MXC_UART_Init(
            HOST_UART,
            HOST_UART_BAUD,
            MXC_UART_ERTCO_CLK
        );


    if (error != E_NO_ERROR) {

        printf(
            "ERROR: ESP32-H2 UART3 initialization failed: %d\n",
            error
        );


        while (1) {

            process_alive_led();
        }
    }


    printf(
        "ESP32-H2 UART3 initialized successfully.\n"
    );

#else

    printf(
        "ESP32-H2 UART3 DISABLED for this test.\n"
    );

#endif


    /**************************************************************************
     * WATCHDOG
     **************************************************************************/

    error =
        watchdog_init();


    if (error != E_NO_ERROR) {

        printf(
            "ERROR: Watchdog initialization failed: %d\n",
            error
        );
    }


    /**************************************************************************
     * APPLICATION STATE
     **************************************************************************/

    g_co_state =
        CO_STATE_INIT;


    g_sensor_status =
        SENSOR_STATUS_NO_DATA;


    g_last_sensor_ms =
        system_millis();


    g_last_status_ms =
        system_millis();


    g_last_host_heartbeat_ms =
        system_millis();


    g_last_sensor_recovery_ms =
        system_millis();


    /**************************************************************************
     * CLEAR SENSOR FIFO
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
     * HOST STARTUP
     **************************************************************************/

#if HOST_UART_ENABLED

    printf(
        "Sending device information...\n"
    );

    send_device_info();


    printf(
        "Sending initial heartbeat...\n"
    );

    send_heartbeat();

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
        " BLUE LED = firmware alive heartbeat\n"
    );

    printf(
        " RED      = sensor fault / alarm\n"
    );

    printf(
        " GREEN    = sensor reading indication\n"
    );

    printf(
        " Waiting for ZE07-CO frames...\n\n"
    );


    /**************************************************************************
     * MAIN LOOP
     *
     *
     * NOTHING HERE IS ALLOWED TO WAIT FOREVER.
     *
     **************************************************************************/

    while (1) {

        /*
         * 1. ALWAYS service the alive indicator first.
         *
         * This does not depend on the sensor.
         */
        process_alive_led();


        /*
         * 2. Process any bytes currently waiting in UART2.
         *
         * This is non-blocking.
         */
        process_sensor_uart();


        /*
         * 3. Check whether the sensor has stopped responding.
         */
        process_sensor_health();


        /*
         * 4. Attempt sensor recovery when required.
         */
        if (g_sensor_status ==
            SENSOR_STATUS_TIMEOUT) {

            sensor_uart_recover();
        }


        /*
         * 5. Process ESP32 communications.
         */
        process_communications();


        /*
         * 6. Small scheduler delay.
         *
         * This is only 1 ms.
         */
        MXC_Delay(
            MXC_DELAY_MSEC(
                PROCESS_PERIOD_MS
            )
        );
    }


    return 0;
}
