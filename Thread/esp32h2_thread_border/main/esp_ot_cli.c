/*
 * ESP32-H2 Thread Telemetry Receiver
 *
 * Node 2:
 *   Thread UDP receiver
 *   Listens on UDP port 55311
 *
 * Receives telemetry from:
 *   Node 1 / CO sensor
 *
 * Payload:
 *   JSON telemetry
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
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
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"

#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/logging.h"

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif

// =========================================================
// LED CONFIGURATION
// =========================================================

#define LED_GPIO            GPIO_NUM_8
#define LED_COUNT           1
#define LED_BRIGHTNESS      32
#define LED_GREEN_BLINK_MS  200

static led_strip_handle_t led_strip;

// =========================================================
// TELEMETRY CONFIGURATION
// =========================================================

#define THREAD_TELEMETRY_PORT 55311

#define TELEMETRY_BUFFER_SIZE 512

#define TAG "telemetry_rx"

// =========================================================
// LED FUNCTIONS
// =========================================================

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
}

// =========================================================
// UDP TELEMETRY RECEIVER
// =========================================================

static void telemetry_receiver_task(void *arg)
{
    char buffer[TELEMETRY_BUFFER_SIZE];

    struct sockaddr_in6 source_addr;
    socklen_t source_addr_len = sizeof(source_addr);

    while (true)
    {
        otInstance *instance = esp_openthread_get_instance();

        if (instance == NULL)
        {
            ESP_LOGW(TAG, "OpenThread instance not available");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        otDeviceRole role = otThreadGetDeviceRole(instance);

        if (role == OT_DEVICE_ROLE_DISABLED ||
            role == OT_DEVICE_ROLE_DETACHED)
        {
            led_red();

            ESP_LOGI(
                TAG,
                "Thread not attached yet, receiver waiting..."
            );

            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(
            TAG,
            "Thread attached. Starting telemetry receiver..."
        );

        // -------------------------------------------------
        // Create IPv6 UDP socket
        // -------------------------------------------------

        int sock = socket(
            AF_INET6,
            SOCK_DGRAM,
            IPPROTO_UDP
        );

        if (sock < 0)
        {
            ESP_LOGE(
                TAG,
                "Failed to create UDP socket: errno %d",
                errno
            );

            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // -------------------------------------------------
        // Bind to all IPv6 addresses on port 55311
        // -------------------------------------------------

        struct sockaddr_in6 local_addr = {0};

        local_addr.sin6_family = AF_INET6;
        local_addr.sin6_addr = in6addr_any;
        local_addr.sin6_port = htons(THREAD_TELEMETRY_PORT);

        int result = bind(
            sock,
            (struct sockaddr *)&local_addr,
            sizeof(local_addr)
        );

        if (result < 0)
        {
            ESP_LOGE(
                TAG,
                "Failed to bind UDP port %d: errno %d",
                THREAD_TELEMETRY_PORT,
                errno
            );

            close(sock);

            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(
            TAG,
            "Telemetry receiver listening on UDP port %d",
            THREAD_TELEMETRY_PORT
        );

        led_blue();

        // -------------------------------------------------
        // Receive telemetry forever
        // -------------------------------------------------

        while (true)
        {
            source_addr_len = sizeof(source_addr);

            int received = recvfrom(
                sock,
                buffer,
                sizeof(buffer) - 1,
                0,
                (struct sockaddr *)&source_addr,
                &source_addr_len
            );

            if (received < 0)
            {
                ESP_LOGE(
                    TAG,
                    "recvfrom() failed: errno %d",
                    errno
                );

                break;
            }

            // Null terminate received JSON
            buffer[received] = '\0';

            // Convert sender IPv6 address to text
            char source_ip[INET6_ADDRSTRLEN];

            if (inet_ntop(
                    AF_INET6,
                    &source_addr.sin6_addr,
                    source_ip,
                    sizeof(source_ip)
                ) == NULL)
            {
                strcpy(source_ip, "unknown");
            }

            uint16_t source_port =
                ntohs(source_addr.sin6_port);

            // -------------------------------------------------
            // TELEMETRY RECEIVED
            // -------------------------------------------------

            ESP_LOGI(
                TAG,
                "========================================"
            );

            ESP_LOGI(
                TAG,
                "TELEMETRY RECEIVED"
            );

            ESP_LOGI(
                TAG,
                "Source: [%s]:%u",
                source_ip,
                source_port
            );

            ESP_LOGI(
                TAG,
                "Payload length: %d bytes",
                received
            );

            ESP_LOGI(
                TAG,
                "Payload: %s",
                buffer
            );

            ESP_LOGI(
                TAG,
                "========================================"
            );

            // Green blink = telemetry received
            led_green_blink();
        }

        close(sock);

        ESP_LOGW(
            TAG,
            "Telemetry receiver socket closed; restarting..."
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// =========================================================
// OPENTHREAD NETIF
// =========================================================

static esp_netif_t *init_openthread_netif(
    const esp_openthread_platform_config_t *config
)
{
    esp_netif_config_t cfg =
        ESP_NETIF_DEFAULT_OPENTHREAD();

    esp_netif_t *netif =
        esp_netif_new(&cfg);

    assert(netif != NULL);

    ESP_ERROR_CHECK(
        esp_netif_attach(
            netif,
            esp_openthread_netif_glue_init(config)
        )
    );

    return netif;
}

// =========================================================
// OPENTHREAD WORKER
// =========================================================

static void ot_task_worker(void *aContext)
{
    esp_openthread_platform_config_t config = {
        .radio_config =
            ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),

        .host_config =
            ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),

        .port_config =
            ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    // -----------------------------------------------------
    // Initialize OpenThread
    // -----------------------------------------------------

    ESP_ERROR_CHECK(
        esp_openthread_init(&config)
    );

#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC

    (void)otLoggingSetLevel(
        CONFIG_LOG_DEFAULT_LEVEL
    );

#endif

    // -----------------------------------------------------
    // Initialize CLI
    // -----------------------------------------------------

#if CONFIG_OPENTHREAD_CLI

    esp_openthread_cli_init();

#endif

    // -----------------------------------------------------
    // Initialize network interface
    // -----------------------------------------------------

    esp_netif_t *openthread_netif =
        init_openthread_netif(&config);

    esp_netif_set_default_netif(
        openthread_netif
    );

    // -----------------------------------------------------
    // Start telemetry receiver
    // -----------------------------------------------------

    xTaskCreate(
        telemetry_receiver_task,
        "telemetry_rx",
        4096,
        NULL,
        4,
        NULL
    );

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION

    esp_cli_custom_command_init();

#endif

    // -----------------------------------------------------
    // Start CLI
    // -----------------------------------------------------

#if CONFIG_OPENTHREAD_CLI

    esp_openthread_cli_create_task();

#endif

    // -----------------------------------------------------
    // START THREAD AUTOMATICALLY FROM STORED DATASET
    // -----------------------------------------------------

    otOperationalDatasetTlvs dataset;

    otError error =
        otDatasetGetActiveTlvs(
            esp_openthread_get_instance(),
            &dataset
        );

    if (error == OT_ERROR_NONE) {

        ESP_LOGI(
            TAG,
            "Starting Thread automatically from stored Active Dataset"
        );

        ESP_ERROR_CHECK(
            esp_openthread_auto_start(&dataset)
        );

    } else {

        ESP_LOGE(
            TAG,
            "No Active Thread Dataset found: %d",
            error
        );
    }

    // -----------------------------------------------------
    // Run OpenThread main loop
    // -----------------------------------------------------

    esp_openthread_launch_mainloop();

    // -----------------------------------------------------
    // Cleanup
    // -----------------------------------------------------

    esp_openthread_netif_glue_deinit();

    esp_netif_destroy(
        openthread_netif
    );

    esp_vfs_eventfd_unregister();

    vTaskDelete(NULL);
}

// =========================================================
// APPLICATION ENTRY
// =========================================================

void app_main(void)
{
    led_init();

    // Red = not attached / waiting
    led_red();

    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(
        nvs_flash_init()
    );

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    ESP_ERROR_CHECK(
        esp_netif_init()
    );

    ESP_ERROR_CHECK(
        esp_vfs_eventfd_register(
            &eventfd_config
        )
    );

    xTaskCreate(
        ot_task_worker,
        "ot_cli_main",
        10240,
        xTaskGetCurrentTaskHandle(),
        5,
        NULL
    );
}