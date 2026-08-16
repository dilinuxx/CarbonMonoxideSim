/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Command Line Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <unistd.h>
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

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

//
#define LED_GPIO GPIO_NUM_8
#define LED_COUNT 1
#define LED_BRIGHTNESS 32
#define LED_GREEN_BLINK_MS 200

static led_strip_handle_t led_strip;

//
#define THREAD_TELEMETRY_PORT 55311
#define TELEMETRY_INTERVAL_MS 10000
#define THREAD_BORDER_ROUTER_IPV6 "fdde:ad00:beef:0:2254:2255:fcbf:2241"
#define DEVICE_ID "CBP-00400"

#define TAG "ot_esp_cli"

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

static float getActualCO(void)
{
    return 6.7f;
}

static float getPredictedCO(void)
{
    return 8.2f;
}

static float getTemperature(void)
{
    return 21.4f;
}

static float getHumidity(void)
{
    return 48.2f;
}

static int getBatteryPercent(void)
{
    return 87;
}

static const char *getTimestamp(void)
{
    return "2026-08-16T09:00:00Z";
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

    xTaskCreate(
        telemetry_task,
        "telemetry_task",
        4096,
        NULL,
        4,
        NULL
    );

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

    // Run the main loop
#if CONFIG_OPENTHREAD_CLI
    esp_openthread_cli_create_task();
#endif
#if CONFIG_OPENTHREAD_AUTO_START
    otOperationalDatasetTlvs dataset;
    otError error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
    ESP_ERROR_CHECK(esp_openthread_auto_start((error == OT_ERROR_NONE) ? &dataset : NULL));
#endif
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
