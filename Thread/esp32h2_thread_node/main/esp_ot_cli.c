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
static int buildTelemetryJSON(char *buffer, size_t buffer_size);

//
static void updateSimulatedSensors(void);
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
static int buildTelemetryJSON(char *buffer, size_t buffer_size)
{
    updateSimulatedSensors();

    float actualCO = getActualCO();
    float predictedCO = getPredictedCO();
    float temperature = getTemperature();
    float humidity = getHumidity();
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
        "\"battery_percent\":%d"
        "}",
        DEVICE_ID,
        getTimestamp(),
        predictedCO,
        actualCO,
        temperature,
        humidity,
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

static float simulated_temperature = 21.4f;
static float simulated_humidity = 48.2f;
static float simulated_actual_co = 6.7f;
static int simulated_battery = 87;

static float random_float(float min, float max)
{
return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

static void updateSimulatedSensors(void)
{
// -----------------------------------------------------
// Temperature
// Normal indoor range: approximately 19 - 24 C
// Small random movement each cycle
// -----------------------------------------------------
simulated_temperature += random_float(-0.4f, 0.4f);

if (simulated_temperature < 19.0f)
    simulated_temperature = 19.0f;

if (simulated_temperature > 24.0f)
    simulated_temperature = 24.0f;


// -----------------------------------------------------
// Humidity
// Normal indoor range: approximately 40 - 60 %
// -----------------------------------------------------
simulated_humidity += random_float(-1.5f, 1.5f);

if (simulated_humidity < 40.0f)
    simulated_humidity = 40.0f;

if (simulated_humidity > 60.0f)
    simulated_humidity = 60.0f;


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
    return simulated_temperature;
}


static float getHumidity(void)
{
    return simulated_humidity;
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
    xTaskCreate(ot_task_worker, "ot_cli_main", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);
}
