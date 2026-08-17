/*
 * ESP32-H2 Thread Telemetry Receiver / Gateway
 *
 * Node 2 - Thread Telemetry Gateway
 *
 * Purpose:
 *   Receives CO sensor telemetry from Node 1 and other
 *   Thread sensor nodes over IPv6 UDP, tracks discovered
 *   sensor nodes, and forwards the received telemetry to
 *   an ESP32-WROOM over UART.
 *
 * Thread communication:
 *   Transport:       IPv6 UDP
 *   Listen port:     55311
 *   Listen address:  All local IPv6 addresses
 *
 * Telemetry flow:
 *
 *   Thread Sensor Node
 *          |
 *          | IPv6 / UDP
 *          | Port 55311
 *          ESP32-WROOM
 *
 * Telemetry processing:
 *   1. Wait for the ESP32-H2 to attach to the Thread network.
 *   2. Create an IPv6 UDP socket.
 *   3. Listen on UDP port 55311.
 *   4. Receive JSON telemetry from Thread sensor nodes.
 *   5. Identify each sensor node by its IPv6 address.
 *   6. Track packet count and payload size for each node.
 *   7. Log the received telemetry.
 *   8. Wrap the telemetry with source information.
 *   9. Forward the resulting JSON message to the ESP32-WROOM
 *      over UART1.
 *
 * Sensor node identification:
 *   The IPv6 address identifies the sensor node.
 *   The UDP source port is not used as the node identity because
 *   a sensor may use a different ephemeral source port for each
 *   telemetry transmission.
 *
 * UART connection to ESP32-WROOM:
 *   UART:       UART1
 *   TX:         GPIO24
 *   RX:         GPIO23
 *   Baud rate:  115200
 *
 * UART message format:
 *
 *   {
 *     "source": "<sensor IPv6 address>",
 *     "source_port": <UDP source port>,
 *     "payload": { ...sensor telemetry... }
 *   }
 *
 * LED status:
 *   RED   = Thread not attached / receiver waiting
 *   BLUE  = Thread attached / receiver ready
 *   GREEN = Telemetry received
 *
 * Thread startup:
 *   The device automatically starts Thread using the stored
 *   Active Operational Dataset.
 *
 * Sensor capacity:
 *   Maximum tracked sensor nodes: 16
 *
 * Role in the system:
 *
 *   Node 1 / CO Sensor
 *          │
 *          │ Thread / IPv6 UDP
 *          ▼
 *   Node 2 / ESP32-H2 Gateway
 *          │
 *          │ UART1
 *          ▼
 *   ESP32-WROOM
 *
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "led_strip.h"
#include "led_strip_rmt.h"
#include "driver/uart.h"

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
// UART TO ESP32-WROOM
// =========================================================

#define WROOM_UART_NUM          UART_NUM_1

#define WROOM_UART_TX_GPIO     GPIO_NUM_24
#define WROOM_UART_RX_GPIO     GPIO_NUM_23

#define WROOM_UART_BAUD_RATE   115200

#define WROOM_UART_TX_BUFFER   1024
#define WROOM_UART_RX_BUFFER   1024

// =========================================================
// TELEMETRY CONFIGURATION
// =========================================================

#define THREAD_TELEMETRY_PORT 55311

#define TELEMETRY_BUFFER_SIZE 512
#define WROOM_UART_MESSAGE_SIZE 768

#define TAG "telemetry_rx"

// =========================================================
// SENSOR NODE TABLE
// =========================================================

#define MAX_SENSOR_NODES 16

typedef struct {
    bool     active;
    char     address[INET6_ADDRSTRLEN];
    uint16_t port;
    uint32_t packets_received;
    int      last_payload_length;
} sensor_node_t;

static sensor_node_t sensor_nodes[MAX_SENSOR_NODES];

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
// SENSOR NODE MANAGEMENT
// =========================================================

static int find_sensor_node(const char *address)
{
    for (int i = 0; i < MAX_SENSOR_NODES; i++) {

        if (!sensor_nodes[i].active) {
            continue;
        }

        if (strcmp(sensor_nodes[i].address, address) == 0) {
            return i;
        }
    }

    return -1;
}

static int add_sensor_node(
    const char *address,
    uint16_t port
)
{
    // Check whether this node already exists
    int existing = find_sensor_node(address);

    if (existing >= 0) {
        return existing;
    }

    // Find an unused slot
    for (int i = 0; i < MAX_SENSOR_NODES; i++) {

        if (!sensor_nodes[i].active) {

            sensor_nodes[i].active = true;

            strncpy(
                sensor_nodes[i].address,
                address,
                sizeof(sensor_nodes[i].address) - 1
            );

            sensor_nodes[i].address[
                sizeof(sensor_nodes[i].address) - 1
            ] = '\0';

            sensor_nodes[i].port = port;
            sensor_nodes[i].packets_received = 0;
            sensor_nodes[i].last_payload_length = 0;

            ESP_LOGI(
                TAG,
                "NEW SENSOR NODE DISCOVERED: [%s]:%u",
                sensor_nodes[i].address,
                sensor_nodes[i].port
            );

            return i;
        }
    }

    ESP_LOGW(
        TAG,
        "Sensor node table full - cannot add [%s]:%u",
        address,
        port
    );

    return -1;
}

static void print_sensor_nodes(void)
{
    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "DISCOVERED SENSOR NODES"
    );

    int count = 0;

    for (int i = 0; i < MAX_SENSOR_NODES; i++) {

        if (!sensor_nodes[i].active) {
            continue;
        }

        count++;

        ESP_LOGI(
            TAG,
            "Node %d: [%s]:%u | packets=%lu | last_len=%d",
            count,
            sensor_nodes[i].address,
            sensor_nodes[i].port,
            (unsigned long)sensor_nodes[i].packets_received,
            sensor_nodes[i].last_payload_length
        );
    }

    if (count == 0) {
        ESP_LOGI(
            TAG,
            "No sensor nodes discovered"
        );
    }

    ESP_LOGI(
        TAG,
        "Total sensor nodes: %d",
        count
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );
}

// =========================================================
// UART INITIALIZATION
// =========================================================

static void wroom_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = WROOM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(
        uart_driver_install(
            WROOM_UART_NUM,
            WROOM_UART_RX_BUFFER,
            WROOM_UART_TX_BUFFER,
            0,
            NULL,
            0
        )
    );

    ESP_ERROR_CHECK(
        uart_param_config(
            WROOM_UART_NUM,
            &uart_config
        )
    );

    ESP_ERROR_CHECK(
        uart_set_pin(
            WROOM_UART_NUM,
            WROOM_UART_TX_GPIO,
            WROOM_UART_RX_GPIO,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        )
    );

    ESP_LOGI(
        TAG,
        "WROOM UART initialized: UART%d TX=GPIO%d RX=GPIO%d baud=%d",
        WROOM_UART_NUM,
        WROOM_UART_TX_GPIO,
        WROOM_UART_RX_GPIO,
        WROOM_UART_BAUD_RATE
    );
}

// =========================================================
// SEND TELEMETRY TO ESP32-WROOM
// =========================================================

static void send_telemetry_to_wroom(
    const char *source_ip,
    uint16_t source_port,
    const char *payload
)
{
    char uart_message[WROOM_UART_MESSAGE_SIZE];

    int length = snprintf(
        uart_message,
        sizeof(uart_message),
        "{\"source\":\"%s\",\"source_port\":%u,\"payload\":%s}\n",
        source_ip,
        source_port,
        payload
    );

    if (length < 0) {

        ESP_LOGE(
            TAG,
            "Failed to format UART telemetry message"
        );

        return;
    }

    if (length >= sizeof(uart_message)) {

        ESP_LOGE(
            TAG,
            "UART telemetry message too large"
        );

        return;
    }

    int written = uart_write_bytes(
        WROOM_UART_NUM,
        uart_message,
        length
    );

    if (written < 0) {

        ESP_LOGE(
            TAG,
            "Failed to send telemetry to WROOM"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Forwarded %d bytes to ESP32-WROOM",
        written
    );
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
            // REGISTER / UPDATE SENSOR NODE
            // -------------------------------------------------

            int node_index = add_sensor_node(
                source_ip,
                source_port
            );

            if (node_index >= 0) {

                sensor_nodes[node_index].packets_received++;

                sensor_nodes[node_index].last_payload_length =
                    received;
            }

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

            // -------------------------------------------------
            // PRINT ALL KNOWN SENSOR NODES
            // -------------------------------------------------
            print_sensor_nodes();

            // -------------------------------------------------
            // FORWARD TELEMETRY TO ESP32-WROOM
            // -------------------------------------------------
            send_telemetry_to_wroom(
                source_ip,
                source_port,
                buffer
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

    // Initialize UART connection to ESP32-WROOM
    wroom_uart_init();


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