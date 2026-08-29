/*
 * ESP32-H2 Thread Telemetry Sender
 *
 * Node 1 — CO Sensor
 *
 * Purpose:
 *   Collects CO sensor telemetry and periodically sends it
 *   over the Thread network to the Node 2 telemetry receiver.
 *
 * Thread communication:
 *   Transport:     IPv6 UDP
 *   Destination:   Node 2 / Thread Border Router
 *   UDP port:      55311
 *   Send interval: 10 seconds
 *
 * Destination:
 *   fdde:ad00:beef:0:6156:1314:fcbf:27b6
 *
 * Telemetry payload:
 *   JSON containing:
 *     - device_id
 *     - timestamp
 *     - sensor_status
 *     - alarm
 *     - predicted_co_ppm
 *     - actual_co_ppm
 *     - temperature
 *     - humidity
 *     - battery_percent
 *
 * LED status:
 *   RED   = Thread device is not attached
 *   BLUE  = Thread attached / ready
 *   GREEN = Telemetry successfully transmitted
 *
 * Current device:
 *   CBP-00336
 *
 * Note:
 *   Sensor values and timestamp are currently simulated by the
 *   get*() functions and can later be replaced by real sensor
 *   and RTC/time sources.
 */

#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_cli.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/uart_types.h"
#include "nvs_flash.h"
#include "openthread/cli.h"
#include "openthread/instance.h"
#include "openthread/logging.h"
#include "openthread/tasklet.h"
#include "openthread/thread.h"
#include "openthread/dataset.h"

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

// =========================================================
// GATEWAY THREAD ACTIVE DATASET
// =========================================================
//
// This is the Active Operational Dataset copied from the
// Thread gateway.
//
//
static const char *THREAD_GATEWAY_DATASET_HEX =
    "0e080000000000000000000300000b35060004001fffe00208"
    "dead00beef00cafe0708fddead00beef00000510b010b6cc59a"
    "946a51b7d8ad6bcc061b7030a4f70656e546872656164010257"
    "e20410e363929d52764efa12fbb8760b498e890c0402a0f7f8";

// =========================================================
// LED CONFIGURATION
// =========================================================
#define LED_GPIO GPIO_NUM_8
#define LED_COUNT 1
#define LED_BRIGHTNESS 32
#define LED_GREEN_BLINK_MS 200

static led_strip_handle_t led_strip;

// =========================================================
// BME280 REGISTERS
// =========================================================

#define BME280_REG_ID        0xD0
#define BME280_REG_RESET     0xE0
#define BME280_REG_CTRL_HUM  0xF2
#define BME280_REG_STATUS    0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG    0xF5

#define BME280_REG_DATA      0xF7

#define BME280_REG_CALIB_00  0x88
#define BME280_REG_CALIB_26  0xE1

#define BME280_CHIP_ID       0x60

// =========================================================
// BME280 CALIBRATION DATA
// =========================================================

static uint16_t dig_T1;
static int16_t  dig_T2;
static int16_t  dig_T3;

static uint16_t dig_P1;
static int16_t  dig_P2;
static int16_t  dig_P3;
static int16_t  dig_P4;
static int16_t  dig_P5;
static int16_t  dig_P6;
static int16_t  dig_P7;
static int16_t  dig_P8;
static int16_t  dig_P9;

static uint8_t  dig_H1;
static int16_t  dig_H2;
static uint8_t  dig_H3;
static int16_t  dig_H4;
static int16_t  dig_H5;
static int8_t   dig_H6;

static int32_t t_fine;

// =========================================================
// BME280 / I2C CONFIGURATION
// =========================================================

static float real_temperature = 0.0f;
static float real_humidity = 0.0f;
static float real_pressure = 0.0f;

/*
 * ESP-IDF i2c_simple example for targets other than
 * ESP32 / ESP32-S2 / ESP32-S3 defaults to:
 *
 *   SDA = GPIO 1
 *   SCL = GPIO 2
 *
 * We therefore use the same GPIO assignment here.
 */

#define I2C_MASTER_SDA_IO       GPIO_NUM_1
#define I2C_MASTER_SCL_IO       GPIO_NUM_2
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000
#define I2C_MASTER_TIMEOUT_MS   1000

/*
 * BME280 modules commonly use either:
 *
 *   0x76
 *   0x77
 *
 * We start with 0x77.
 */
#define BME280_I2C_ADDRESS      0x77

static i2c_master_bus_handle_t bme280_i2c_bus = NULL;
static i2c_master_dev_handle_t bme280_i2c_dev = NULL;

static esp_err_t bme280_init(void);
static esp_err_t bme280_read(float *temperature, float *humidity, float *pressure);
static void updateBME280(void);
static void i2c_scan(void);
static esp_err_t bme280_i2c_bus_init(void);
static esp_err_t bme280_i2c_device_init(void);

// =========================================================
// THREAD TELEMETRY CONFIGURATION
// =========================================================
#define THREAD_TELEMETRY_PORT 55311
#define TELEMETRY_INTERVAL_MS 10000
#define THREAD_BORDER_ROUTER_IPV6 "fdde:ad00:beef:0:6156:1314:fcbf:27b6"
//#define DEVICE_ID "CBP-00400"       // Node 1
#define DEVICE_ID "CBP-00336"         // Node 2
//#define DEVICE_ID "CBP-00212"       // Node 3

#define TAG "ot_esp_cli"

//
static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_size);

//
static void led_init(void);
static void led_off(void);
static void led_red(void);
static void led_blue(void);
static void led_green_blink(void);

//
static float getActualCO(void);
static float getPredictedCO(void);
static float getTemperature(void);
static float getHumidity(void);
static int getBatteryPercent(void);
static const char *getTimestamp(void);
static void updateSimulatedSensors(void);
static int buildTelemetryJSON(char *buffer, size_t buffer_size);

//
static float random_float(float min, float max);

static int hex_to_bytes(
    const char *hex,
    uint8_t *out,
    size_t out_size)
{
    size_t hex_len = strlen(hex);

    if ((hex_len % 2) != 0) {
        ESP_LOGE(TAG, "Dataset hex string has odd length");
        return -1;
    }

    size_t byte_len = hex_len / 2;

    if (byte_len > out_size) {
        ESP_LOGE(
            TAG,
            "Dataset too large: %u bytes, max %u",
            (unsigned)byte_len,
            (unsigned)out_size
        );
        return -1;
    }

    for (size_t i = 0; i < byte_len; i++) {

        unsigned int value;

        if (sscanf(&hex[i * 2], "%2x", &value) != 1) {
            ESP_LOGE(
                TAG,
                "Invalid dataset hex at position %u",
                (unsigned)(i * 2)
            );
            return -1;
        }

        out[i] = (uint8_t)value;
    }

    return (int)byte_len;
}

static int load_gateway_dataset(otOperationalDatasetTlvs *dataset)
{
    memset(dataset, 0, sizeof(*dataset));

    int length = hex_to_bytes(
        THREAD_GATEWAY_DATASET_HEX,
        dataset->mTlvs,
        sizeof(dataset->mTlvs)
    );

    if (length < 0) {
        return -1;
    }

    dataset->mLength = (uint8_t)length;

    ESP_LOGI(
        TAG,
        "Loaded gateway Thread dataset: %d bytes",
        length
    );

    return 0;
}

static void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &led_strip
        )
    );

    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_off(void)
{
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_red(void)
{
    ESP_ERROR_CHECK(
        led_strip_set_pixel(
            led_strip,
            0,
            LED_BRIGHTNESS,
            0,
            0
        )
    );

    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_blue(void)
{
    ESP_ERROR_CHECK(
        led_strip_set_pixel(
            led_strip,
            0,
            0,
            0,
            LED_BRIGHTNESS
        )
    );

    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_green_blink(void)
{
    ESP_ERROR_CHECK(
        led_strip_set_pixel(
            led_strip,
            0,
            0,
            LED_BRIGHTNESS,
            0
        )
    );

    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    vTaskDelay(pdMS_TO_TICKS(LED_GREEN_BLINK_MS));

    led_blue();

    //led_off();
}

//
static esp_err_t bme280_i2c_bus_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus...");

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(
        &bus_config,
        &bme280_i2c_bus
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize I2C bus: %s",
            esp_err_to_name(err)
        );
        return err;
    }

    ESP_LOGI(
        TAG,
        "I2C bus initialized: SDA=%d SCL=%d",
        I2C_MASTER_SDA_IO,
        I2C_MASTER_SCL_IO
    );

    return ESP_OK;
}

//
static void i2c_scan(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus...");

    if (bme280_i2c_bus == NULL) {
        ESP_LOGE(TAG, "I2C scan aborted: bus handle is NULL");
        return;
    }

    int found = 0;

    for (uint8_t address = 1; address < 127; address++) {

        esp_err_t err = i2c_master_probe(
            bme280_i2c_bus,
            address,
            I2C_MASTER_TIMEOUT_MS
        );

        if (err == ESP_OK) {

            ESP_LOGI(
                TAG,
                "I2C device found at address 0x%02X",
                address
            );

            found++;
        }
    }

    if (found == 0) {
        ESP_LOGW(
            TAG,
            "No I2C devices found"
        );
    }

    ESP_LOGI(
        TAG,
        "I2C scan complete: %d device(s) found",
        found
    );
}

//
static esp_err_t bme280_i2c_device_init(void)
{
    if (bme280_i2c_bus == NULL) {
        ESP_LOGE(TAG, "Cannot add BME280: I2C bus is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(
        bme280_i2c_bus,
        &device_config,
        &bme280_i2c_dev
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "BME280 device init failed: %s",
            esp_err_to_name(err)
        );
        return err;
    }

    ESP_LOGI(
        TAG,
        "BME280 device added at address 0x%02X",
        BME280_I2C_ADDRESS
    );

    return ESP_OK;
}

//
static esp_err_t bme280_read_register(
    uint8_t reg,
    uint8_t *data,
    size_t length)
{
    if (bme280_i2c_dev == NULL) {
        ESP_LOGE(TAG, "BME280 read failed: device handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(
        bme280_i2c_dev,
        &reg,
        1,
        data,
        length,
        I2C_MASTER_TIMEOUT_MS
    );
}

//
static esp_err_t bme280_write_register(
    uint8_t reg,
    uint8_t value)
{
    if (bme280_i2c_dev == NULL) {
        ESP_LOGE(TAG, "BME280 write failed: device handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[2] = {
        reg,
        value
    };

    return i2c_master_transmit(
        bme280_i2c_dev,
        data,
        sizeof(data),
        I2C_MASTER_TIMEOUT_MS
    );
}

static esp_err_t bme280_read_calibration(void)
{
    uint8_t calib1[26];
    uint8_t calib2[7];

    esp_err_t err;

    err = bme280_read_register(
        BME280_REG_CALIB_00,
        calib1,
        sizeof(calib1)
    );

    if (err != ESP_OK) {
        return err;
    }

    err = bme280_read_register(
        BME280_REG_CALIB_26,
        calib2,
        sizeof(calib2)
    );

    if (err != ESP_OK) {
        return err;
    }

    // -----------------------------------------------------
    // Temperature calibration
    // -----------------------------------------------------

    dig_T1 = (uint16_t)(calib1[1] << 8 | calib1[0]);
    dig_T2 = (int16_t)(calib1[3] << 8 | calib1[2]);
    dig_T3 = (int16_t)(calib1[5] << 8 | calib1[4]);

    // -----------------------------------------------------
    // Pressure calibration
    // -----------------------------------------------------

    dig_P1 = (uint16_t)(calib1[7] << 8 | calib1[6]);
    dig_P2 = (int16_t)(calib1[9] << 8 | calib1[8]);
    dig_P3 = (int16_t)(calib1[11] << 8 | calib1[10]);
    dig_P4 = (int16_t)(calib1[13] << 8 | calib1[12]);
    dig_P5 = (int16_t)(calib1[15] << 8 | calib1[14]);
    dig_P6 = (int16_t)(calib1[17] << 8 | calib1[16]);
    dig_P7 = (int16_t)(calib1[19] << 8 | calib1[18]);
    dig_P8 = (int16_t)(calib1[21] << 8 | calib1[20]);
    dig_P9 = (int16_t)(calib1[23] << 8 | calib1[22]);

    // -----------------------------------------------------
    // Humidity calibration
    // -----------------------------------------------------

    dig_H1 = calib1[25];

    dig_H2 = (int16_t)(calib2[1] << 8 | calib2[0]);
    dig_H3 = calib2[2];

    dig_H4 = (int16_t)((calib2[3] << 4) |
                       (calib2[4] & 0x0F));

    dig_H5 = (int16_t)((calib2[5] << 4) |
                       (calib2[4] >> 4));

    dig_H6 = (int8_t)calib2[6];

    ESP_LOGI(TAG, "BME280 calibration data loaded");

    return ESP_OK;
}

static esp_err_t bme280_init(void)
{
    ESP_LOGI(TAG, "Initializing BME280 I2C...");

    /*
     * The I2C bus has already been created and scanned
     * by bme280_i2c_bus_init().
     *
     * We must NOT create another I2C bus here.
     */

    if (bme280_i2c_bus == NULL) {
        ESP_LOGE(TAG, "BME280 initialization failed: I2C bus is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    if (bme280_i2c_dev == NULL) {
        esp_err_t err = bme280_i2c_device_init();

        if (err != ESP_OK) {
            return err;
        }
    }

    // -----------------------------------------------------
    // Check chip ID
    // -----------------------------------------------------

    uint8_t chip_id = 0;

    esp_err_t err = bme280_read_register(
        BME280_REG_ID,
        &chip_id,
        1
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read BME280 chip ID: %s",
            esp_err_to_name(err)
        );
        return err;
    }

    ESP_LOGI(
        TAG,
        "BME280 chip ID: 0x%02X",
        chip_id
    );

    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(
            TAG,
            "Unexpected BME280 chip ID! Expected 0x60"
        );
        return ESP_ERR_NOT_FOUND;
    }

    // -----------------------------------------------------
    // Read calibration
    // -----------------------------------------------------

    err = bme280_read_calibration();

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read BME280 calibration"
        );
        return err;
    }

    // -----------------------------------------------------
    // Configure humidity
    //
    // osrs_h = 1
    // -----------------------------------------------------

    err = bme280_write_register(
        BME280_REG_CTRL_HUM,
        0x01
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure BME280 humidity: %s",
            esp_err_to_name(err)
        );
        return err;
    }

    // -----------------------------------------------------
    // Configure measurement
    //
    // osrs_t = 1
    // osrs_p = 1
    // mode   = normal
    //
    // 001 001 11
    // 0x27
    // -----------------------------------------------------

    err = bme280_write_register(
        BME280_REG_CTRL_MEAS,
        0x27
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure BME280 measurement: %s",
            esp_err_to_name(err)
        );
        return err;
    }

    ESP_LOGI(TAG, "BME280 initialized successfully");

    return ESP_OK;
}

//
static float bme280_compensate_temperature(
    int32_t adc_T)
{
    int32_t var1;
    int32_t var2;
    int32_t T;

    var1 =
        ((((adc_T >> 3) -
           ((int32_t)dig_T1 << 1))) *
         ((int32_t)dig_T2)) >> 11;

    var2 =
        (((((adc_T >> 4) -
            ((int32_t)dig_T1)) *
           (adc_T >> 4) -
           ((int32_t)dig_T1)) >> 12) *
         ((int32_t)dig_T3)) >> 14;

    t_fine = var1 + var2;

    T = (t_fine * 5 + 128) >> 8;

    return T / 100.0f;
}

//
static float bme280_compensate_pressure(
    int32_t adc_P)
{
    int64_t var1;
    int64_t var2;
    int64_t p;

    var1 = ((int64_t)t_fine) - 128000;

    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 = var2 +
           ((var1 * (int64_t)dig_P5) << 17);
    var2 = var2 +
           (((int64_t)dig_P4) << 35);

    var1 =
        ((var1 * var1 * (int64_t)dig_P3) >> 8) +
        ((var1 * (int64_t)dig_P2) << 12);

    var1 =
        (((((int64_t)1) << 47) + var1) *
         ((int64_t)dig_P1)) >> 33;

    if (var1 == 0) {
        return 0.0f;
    }

    p = 1048576 - adc_P;

    p = (((p << 31) - var2) * 3125) / var1;

    var1 =
        ((int64_t)dig_P9 *
         (p >> 13) *
         (p >> 13)) >> 25;

    var2 =
        ((int64_t)dig_P8 * p) >> 19;

    p =
        ((p + var1 + var2) >> 8) +
        (((int64_t)dig_P7) << 4);

    return (float)p / 25600.0f;
}

//
static float bme280_compensate_humidity(
    int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r =
        t_fine - ((int32_t)76800);

    v_x1_u32r =
        (((((adc_H << 14) -
            (((int32_t)dig_H4) << 20) -
            (((int32_t)dig_H5) * v_x1_u32r)) +
           ((int32_t)16384)) >> 15) *
         (((((((v_x1_u32r *
               ((int32_t)dig_H6)) >> 10) *
              (((v_x1_u32r *
                 ((int32_t)dig_H3)) >> 11) +
               ((int32_t)32768))) >> 10) +
            ((int32_t)2097152)) *
          ((int32_t)dig_H2) + 8192) >> 14));

    v_x1_u32r =
        v_x1_u32r -
        (((((v_x1_u32r >> 15) *
            (v_x1_u32r >> 15)) >> 7) *
          ((int32_t)dig_H1)) >> 4);

    if (v_x1_u32r < 0)
        v_x1_u32r = 0;

    if (v_x1_u32r > 419430400)
        v_x1_u32r = 419430400;

    return (v_x1_u32r >> 12) / 1024.0f;
}

//
static esp_err_t bme280_read(
    float *temperature,
    float *humidity,
    float *pressure)
{
    uint8_t data[8];

    esp_err_t err = bme280_read_register(
        BME280_REG_DATA,
        data,
        sizeof(data)
    );

    if (err != ESP_OK) {
        return err;
    }

    // -----------------------------------------------------
    // Raw temperature
    // -----------------------------------------------------

    int32_t adc_temperature =
        ((int32_t)data[3] << 12) |
        ((int32_t)data[4] << 4) |
        ((int32_t)data[5] >> 4);

    // -----------------------------------------------------
    // Raw pressure
    // -----------------------------------------------------

    int32_t adc_pressure =
        ((int32_t)data[0] << 12) |
        ((int32_t)data[1] << 4) |
        ((int32_t)data[2] >> 4);

    // -----------------------------------------------------
    // Raw humidity
    // -----------------------------------------------------

    int32_t adc_humidity =
        ((int32_t)data[6] << 8) |
        data[7];

    // -----------------------------------------------------
    // Compensated temperature
    // -----------------------------------------------------

    *temperature =
        bme280_compensate_temperature(
            adc_temperature
        );

    // -----------------------------------------------------
    // Compensated pressure
    // -----------------------------------------------------

    *pressure =
        bme280_compensate_pressure(
            adc_pressure
        );

    // -----------------------------------------------------
    // Compensated humidity
    // -----------------------------------------------------

    *humidity =
        bme280_compensate_humidity(
            adc_humidity
        );

    return ESP_OK;
}

//
static int buildTelemetryJSON(char *buffer, size_t buffer_size)
{
    updateBME280();

    updateSimulatedSensors();

    float actualCO = getActualCO();
    float predictedCO = getPredictedCO();
    float temperature = getTemperature();
    float humidity = getHumidity();
    float pressure = real_pressure;
    int battery = getBatteryPercent();

    int length = snprintf(
        buffer,
        buffer_size,
        "{"
        "\"device_id\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"sensor_status\":\"online\","
        "\"alarm\":false,"
        "\"predicted_co_ppm\":%.1f,"
        "\"actual_co_ppm\":%.1f,"
        "\"temperature\":%.1f,"
        "\"humidity\":%.1f,"
        "\"pressure\":%.2f,"
        "\"battery_percent\":%d"
        "}",
        DEVICE_ID,
        getTimestamp(),
        predictedCO,
        actualCO,
        temperature,
        humidity,
        pressure,
        battery
    );

    if (length < 0 || (size_t)length >= buffer_size) {
        ESP_LOGE(TAG, "Telemetry JSON buffer too small");
        return -1;
    }

    return length;
}

// =========================================================
// SIMULATED SENSOR DATA
// =========================================================
//
// These values are simulated for demonstration purposes.
// They change gradually so that the telemetry looks like
// a real sensor rather than completely random data.
//

static float simulated_actual_co = 6.7f;
static int simulated_battery = 87;

static float random_float(float min, float max)
{
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

static void updateSimulatedSensors(void)
{  
// -----------------------------------------------------
// Actual CO
//
// Typical demonstration range:
// approximately 4 - 12 ppm
//
// The relatively small changes make it look like a
// continuously monitored sensor.
// -----------------------------------------------------
simulated_actual_co += random_float(-1.0f, 1.0f);

if (simulated_actual_co < 4.0f)
    simulated_actual_co = 4.0f;

if (simulated_actual_co > 12.0f)
    simulated_actual_co = 12.0f;


// -----------------------------------------------------
// Battery
//
// Slowly decreases to demonstrate changing telemetry.
// For a demo, reset to 100% after reaching 20%.
// -----------------------------------------------------
if ((rand() % 10) == 0) {
    simulated_battery--;

    if (simulated_battery < 20)
        simulated_battery = 100;
}
}

static void updateBME280(void)
{
    esp_err_t err = bme280_read(
        &real_temperature,
        &real_humidity,
        &real_pressure
    );

    if (err != ESP_OK) {

        ESP_LOGE(TAG,
                 "Failed to read BME280: %s",
                 esp_err_to_name(err));

        return;
    }

    ESP_LOGI(TAG,
             "BME280: Temperature=%.2f C, Humidity=%.2f %%, Pressure=%.2f hPa",
             real_temperature,
             real_humidity,
             real_pressure);
}


static float getActualCO(void)
{
    return simulated_actual_co;
}


static float getPredictedCO(void)
{
    // Simulate an AI/model prediction that is close to the
    // measured CO value but not exactly identical.

    float prediction =
        simulated_actual_co + random_float(-0.8f, 0.8f);

    if (prediction < 0.0f)
        prediction = 0.0f;

    return prediction;
}

static float getTemperature(void)
{
    return real_temperature;
}


static float getHumidity(void)
{
    return real_humidity;
}


static int getBatteryPercent(void)
{
    return simulated_battery;
}


static const char *getTimestamp(void)
{
    static char timestamp[32];

    time_t now;
    struct tm timeinfo;

    time(&now);

    gmtime_r(&now, &timeinfo);

    strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%dT%H:%M:%SZ",
        &timeinfo
    );

    return timestamp;
}

static void sendTelemetry(void)
{
    char json[512];

    int json_length = buildTelemetryJSON(json, sizeof(json));

    if (json_length < 0) {
        return;
    }

    struct sockaddr_in6 destination = {0};

    destination.sin6_family = AF_INET6;
    destination.sin6_port = htons(THREAD_TELEMETRY_PORT);

    if (inet_pton(AF_INET6, THREAD_BORDER_ROUTER_IPV6,
                  &destination.sin6_addr) != 1) {
        ESP_LOGE(TAG, "Invalid Node 2 IPv6 address: %s",
                 THREAD_BORDER_ROUTER_IPV6);
        return;
    }

    int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
        return;
    }

    int result = sendto(
        sock,
        json,
        json_length,
        0,
        (struct sockaddr *)&destination,
        sizeof(destination)
    );

    if (result < 0) {

        ESP_LOGE(TAG,
                "Failed to send telemetry: errno %d",
                errno);

    } else {

        ESP_LOGI(TAG,
                "Telemetry sent to [%s]:%d (%d bytes)",
                THREAD_BORDER_ROUTER_IPV6,
                THREAD_TELEMETRY_PORT,
                result);

        ESP_LOGI(TAG, "Payload: %s", json);

        led_green_blink();
    }

    close(sock);
}

static void telemetry_task(void *arg)
{
    while (true) {

        otInstance *instance = esp_openthread_get_instance();

        if (instance != NULL) {

            otDeviceRole role = otThreadGetDeviceRole(instance);

            if (role != OT_DEVICE_ROLE_DISABLED &&
                role != OT_DEVICE_ROLE_DETACHED) {

                led_blue();

                sendTelemetry();

            } else {

                led_red();

                ESP_LOGI(TAG,
                         "Thread not attached yet, telemetry waiting...");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
    }
}

static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}

static void ot_task_worker(void *aContext)
{
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    // Initialize the OpenThread stack
    ESP_ERROR_CHECK(esp_openthread_init(&config));

#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    // The OpenThread log level directly matches ESP log level
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif

    // Initialize the OpenThread cli
#if CONFIG_OPENTHREAD_CLI
    esp_openthread_cli_init();
#endif

    esp_netif_t *openthread_netif;

    // Initialize the esp_netif bindings
    openthread_netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(openthread_netif);

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

    // Run the main loop
#if CONFIG_OPENTHREAD_CLI
    esp_openthread_cli_create_task();
#endif

    // =====================================================
    // START THREAD AUTOMATICALLY
    // =====================================================

    ESP_LOGI(
        TAG,
        "Starting Thread automatically from Kconfig dataset"
    );

    otOperationalDatasetTlvs dataset;

    if (load_gateway_dataset(&dataset) != 0) {
        ESP_LOGE(TAG, "Failed to load gateway Thread dataset");
        abort();
    }

    ESP_LOGI(
        TAG,
        "Starting Thread using gateway Active Operational Dataset"
    );

    ESP_ERROR_CHECK(
        esp_openthread_auto_start(&dataset)
    );

    xTaskCreate(
        telemetry_task,
        "telemetry_task",
        4096,
        NULL,
        4,
        NULL
    );

    esp_openthread_launch_mainloop();

    // Clean up
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);

    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

void app_main(void)
{
    //
    led_init();
    led_red();

    // Used eventfds:
    // * netif
    // * ot task queue
    // * radio driver
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    // =====================================================
    // INITIALIZE I2C BUS
    // =====================================================

    ESP_ERROR_CHECK(
        bme280_i2c_bus_init()
    );

    // =====================================================
    // SCAN I2C BUS
    // =====================================================

    i2c_scan();

    // =====================================================
    // INITIALIZE BME280
    // =====================================================

    esp_err_t bme_err = bme280_init();

    if (bme_err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "BME280 initialization failed: %s",
            esp_err_to_name(bme_err)
        );

        ESP_LOGE(
            TAG,
            "Thread telemetry will continue, but temperature/humidity will remain invalid."
        );

    } else {

        ESP_LOGI(
            TAG,
            "BME280 sensor ready."
        );
    }

    xTaskCreate(
        ot_task_worker,
        "ot_cli_main",
        10240,
        xTaskGetCurrentTaskHandle(),
        5,
        NULL
    );
}