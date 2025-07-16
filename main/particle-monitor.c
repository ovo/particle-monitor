#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define UART_PORT_NUM      UART_NUM_1
#define BUF_SIZE           256
#define RD_BUF_SIZE        (BUF_SIZE)
#define RXD_PIN            GPIO_NUM_17
#define TXD_PIN            GPIO_NUM_16

#define WIFI_SSID          "YOUR_WIFI_SSID"        // Your WiFi network name
#define WIFI_PASS          "YOUR_WIFI_PASSWORD"    // Your WiFi password
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "PMS5003";
static EventGroupHandle_t s_wifi_event_group;
static httpd_handle_t server = NULL;

typedef struct {
    uint16_t pm1_0_cf1;
    uint16_t pm2_5_cf1;
    uint16_t pm10_cf1;
    uint16_t pm1_0_ae;
    uint16_t pm2_5_ae;
    uint16_t pm10_ae;
    uint16_t count_03um;
    uint16_t count_05um;
    uint16_t count_10um;
    uint16_t count_25um;
    uint16_t count_50um;
    uint16_t count_100um;
} pms_data_t;

static pms_data_t current_data = {0};


uint16_t to_uint16(uint8_t high, uint8_t low) {
    return ((uint16_t)high << 8) | low;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void parse_pms_frame(const uint8_t *frame) {
    if (frame[0] != 0x42 || frame[1] != 0x4D) return;
    if (to_uint16(frame[2], frame[3]) != 28) return;

    current_data.pm1_0_cf1 = to_uint16(frame[4], frame[5]);
    current_data.pm2_5_cf1 = to_uint16(frame[6], frame[7]);
    current_data.pm10_cf1  = to_uint16(frame[8], frame[9]);

    current_data.pm1_0_ae  = to_uint16(frame[10], frame[11]);
    current_data.pm2_5_ae  = to_uint16(frame[12], frame[13]);
    current_data.pm10_ae   = to_uint16(frame[14], frame[15]);

    current_data.count_03um = to_uint16(frame[16], frame[17]);
    current_data.count_05um = to_uint16(frame[18], frame[19]);
    current_data.count_10um = to_uint16(frame[20], frame[21]);
    current_data.count_25um = to_uint16(frame[22], frame[23]);
    current_data.count_50um = to_uint16(frame[24], frame[25]);
    current_data.count_100um = to_uint16(frame[26], frame[27]);

    ESP_LOGI("PMS5003", "PM_CF: 1.0=%3d  2.5=%3d  10=%3d", current_data.pm1_0_cf1, current_data.pm2_5_cf1, current_data.pm10_cf1);
    ESP_LOGI("PMS5003", "PM_AE: 1.0=%3d  2.5=%3d  10=%3d", current_data.pm1_0_ae, current_data.pm2_5_ae, current_data.pm10_ae);
    ESP_LOGI("PMS5003", "Counts per 0.1L air (particles):");
    ESP_LOGI("PMS5003", " ≥0.3µm=%5d  ≥0.5µm=%5d  ≥1.0µm=%5d", current_data.count_03um, current_data.count_05um, current_data.count_10um);
    ESP_LOGI("PMS5003", " ≥2.5µm=%5d  ≥5.0µm=%5d  ≥10µm =%5d", current_data.count_25um, current_data.count_50um, current_data.count_100um);
}


static esp_err_t api_data_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    cJSON *json = cJSON_CreateObject();
    cJSON *pm_cf = cJSON_CreateObject();
    cJSON *pm_ae = cJSON_CreateObject();
    cJSON *counts = cJSON_CreateObject();

    cJSON_AddNumberToObject(pm_cf, "pm1_0", current_data.pm1_0_cf1);
    cJSON_AddNumberToObject(pm_cf, "pm2_5", current_data.pm2_5_cf1);
    cJSON_AddNumberToObject(pm_cf, "pm10", current_data.pm10_cf1);

    cJSON_AddNumberToObject(pm_ae, "pm1_0", current_data.pm1_0_ae);
    cJSON_AddNumberToObject(pm_ae, "pm2_5", current_data.pm2_5_ae);
    cJSON_AddNumberToObject(pm_ae, "pm10", current_data.pm10_ae);

    cJSON_AddNumberToObject(counts, "count_03um", current_data.count_03um);
    cJSON_AddNumberToObject(counts, "count_05um", current_data.count_05um);
    cJSON_AddNumberToObject(counts, "count_10um", current_data.count_10um);
    cJSON_AddNumberToObject(counts, "count_25um", current_data.count_25um);
    cJSON_AddNumberToObject(counts, "count_50um", current_data.count_50um);
    cJSON_AddNumberToObject(counts, "count_100um", current_data.count_100um);

    cJSON_AddItemToObject(json, "pm_cf", pm_cf);
    cJSON_AddItemToObject(json, "pm_ae", pm_ae);
    cJSON_AddItemToObject(json, "counts", counts);
    cJSON_AddNumberToObject(json, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);

    char *json_string = cJSON_Print(json);
    httpd_resp_send(req, json_string, strlen(json_string));

    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char html[] = "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "    <meta charset=\"UTF-8\">\n"
    "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
    "    <title>Air Quality Monitor</title>\n"
    "    <style>\n"
    "        body {\n"
    "            font-family: Arial, sans-serif;\n"
    "            margin: 20px;\n"
    "            background: #f0f0f0;\n"
    "        }\n"
    "        .container {\n"
    "            max-width: 1200px;\n"
    "            margin: 0 auto;\n"
    "        }\n"
    "        .header {\n"
    "            text-align: center;\n"
    "            margin-bottom: 30px;\n"
    "        }\n"
    "        .metrics {\n"
    "            display: grid;\n"
    "            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n"
    "            gap: 20px;\n"
    "        }\n"
    "        .card {\n"
    "            background: white;\n"
    "            padding: 20px;\n"
    "            border-radius: 8px;\n"
    "            box-shadow: 0 2px 10px rgba(0,0,0,0.1);\n"
    "        }\n"
    "        .card h3 {\n"
    "            margin: 0 0 15px 0;\n"
    "            color: #333;\n"
    "        }\n"
    "        .value {\n"
    "            font-size: 2em;\n"
    "            font-weight: bold;\n"
    "            margin: 10px 0;\n"
    "        }\n"
    "        .unit {\n"
    "            font-size: 0.8em;\n"
    "            color: #666;\n"
    "        }\n"
    "        .pm25 {\n"
    "            color: #e74c3c;\n"
    "        }\n"
    "        .pm10 {\n"
    "            color: #f39c12;\n"
    "        }\n"
    "        .pm1 {\n"
    "            color: #3498db;\n"
    "        }\n"
    "        .status {\n"
    "            text-align: center;\n"
    "            margin: 20px 0;\n"
    "        }\n"
    "        .good {\n"
    "            color: #27ae60;\n"
    "        }\n"
    "        .moderate {\n"
    "            color: #f39c12;\n"
    "        }\n"
    "        .unhealthy {\n"
    "            color: #e74c3c;\n"
    "        }\n"
    "        .timestamp {\n"
    "            text-align: center;\n"
    "            color: #666;\n"
    "            font-size: 0.9em;\n"
    "        }\n"
    "        .loading {\n"
    "            text-align: center;\n"
    "            padding: 40px;\n"
    "            color: #666;\n"
    "        }\n"
    "        .error {\n"
    "            background: #fee;\n"
    "            color: #c00;\n"
    "            padding: 20px;\n"
    "            text-align: center;\n"
    "            border-radius: 8px;\n"
    "            margin: 20px;\n"
    "        }\n"
    "    </style>\n"
    "</head>\n"
    "<body>\n"
    "    <div class=\"container\">\n"
    "        <div class=\"header\">\n"
    "            <h1>&#x1F331; Air Quality Monitor</h1>\n"
    "            <div class=\"status\" id=\"status\">Connecting...</div>\n"
    "        </div>\n"
    "\n"
    "        <div id=\"content\">\n"
    "            <div class=\"loading\">Loading air quality data...</div>\n"
    "        </div>\n"
    "\n"
    "        <div class=\"timestamp\" id=\"timestamp\">Last updated: --</div>\n"
    "    </div>\n"
    "\n"
    "    <script>\n"
    "        let isOnline = false;\n"
    "        let lastUpdateTime = null;\n"
    "\n"
    "        function updateStatus(pm25) {\n"
    "            const status = document.getElementById('status');\n"
    "\n"
    "            if (!isOnline) {\n"
    "                status.innerHTML = '&#x274C; Connection Error';\n"
    "                status.className = 'status unhealthy';\n"
    "                return;\n"
    "            }\n"
    "\n"
    "            if (pm25 <= 12) {\n"
    "                status.innerHTML = '&#x2705; Good';\n"
    "                status.className = 'status good';\n"
    "            } else if (pm25 <= 35) {\n"
    "                status.innerHTML = '&#x26A0; Moderate';\n"
    "                status.className = 'status moderate';\n"
    "            } else {\n"
    "                status.innerHTML = '&#x274C; Unhealthy';\n"
    "                status.className = 'status unhealthy';\n"
    "            }\n"
    "        }\n"
    "\n"
    "        function renderMetrics(data) {\n"
    "            const content = document.getElementById('content');\n"
    "\n"
    "            content.innerHTML = `\n"
    "                <div class=\"metrics\">\n"
    "                    <div class=\"card pm25\">\n"
    "                        <h3>PM2.5</h3>\n"
    "                        <div class=\"value\">${data.pm_ae.pm2_5}</div>\n"
    "                        <div class=\"unit\">μg/m³</div>\n"
    "                    </div>\n"
    "\n"
    "                    <div class=\"card pm10\">\n"
    "                        <h3>PM10</h3>\n"
    "                        <div class=\"value\">${data.pm_ae.pm10}</div>\n"
    "                        <div class=\"unit\">μg/m³</div>\n"
    "                    </div>\n"
    "\n"
    "                    <div class=\"card pm1\">\n"
    "                        <h3>PM1.0</h3>\n"
    "                        <div class=\"value\">${data.pm_ae.pm1_0}</div>\n"
    "                        <div class=\"unit\">μg/m³</div>\n"
    "                    </div>\n"
    "\n"
    "                    <div class=\"card\">\n"
    "                        <h3>Particle Count (≥0.3μm)</h3>\n"
    "                        <div class=\"value\">${data.counts.count_03um}</div>\n"
    "                        <div class=\"unit\">per 0.1L</div>\n"
    "                    </div>\n"
    "\n"
    "                    <div class=\"card\">\n"
    "                        <h3>Particle Count (≥2.5μm)</h3>\n"
    "                        <div class=\"value\">${data.counts.count_25um}</div>\n"
    "                        <div class=\"unit\">per 0.1L</div>\n"
    "                    </div>\n"
    "\n"
    "                    <div class=\"card\">\n"
    "                        <h3>Particle Count (≥10μm)</h3>\n"
    "                        <div class=\"value\">${data.counts.count_100um}</div>\n"
    "                        <div class=\"unit\">per 0.1L</div>\n"
    "                    </div>\n"
    "                </div>\n"
    "            `;\n"
    "        }\n"
    "\n"
    "        function renderError(message) {\n"
    "            const content = document.getElementById('content');\n"
    "            content.innerHTML = `<div class=\"error\">${message}</div>`;\n"
    "        }\n"
    "\n"
    "        async function fetchData() {\n"
    "            try {\n"
    "                const response = await fetch('/api/data');\n"
    "\n"
    "                if (!response.ok) {\n"
    "                    throw new Error(`HTTP ${response.status}: ${response.statusText}`);\n"
    "                }\n"
    "\n"
    "                const data = await response.json();\n"
    "\n"
    "                if (data.error) {\n"
    "                    throw new Error(data.error);\n"
    "                }\n"
    "\n"
    "                isOnline = true;\n"
    "                lastUpdateTime = new Date();\n"
    "\n"
    "                renderMetrics(data);\n"
    "                updateStatus(data.pm_ae.pm2_5);\n"
    "\n"
    "                document.getElementById('timestamp').textContent =\n"
    "                    `Last updated: ${lastUpdateTime.toLocaleString()}`;\n"
    "\n"
    "            } catch (error) {\n"
    "                console.error('Error fetching data:', error);\n"
    "                isOnline = false;\n"
    "                updateStatus(0);\n"
    "\n"
    "                if (document.getElementById('content').innerHTML.includes('Loading')) {\n"
    "                    renderError(`Failed to load data: ${error.message}`);\n"
    "                }\n"
    "\n"
    "                document.getElementById('timestamp').textContent =\n"
    "                    `Connection error: ${error.message}`;\n"
    "            }\n"
    "        }\n"
    "\n"
    "        // Initial load\n"
    "        fetchData();\n"
    "\n"
    "        // Update every 2 seconds for realtime display\n"
    "        setInterval(fetchData, 2000);\n"
    "\n"
    "        // Check connection status every 10 seconds\n"
    "        setInterval(() => {\n"
    "            if (lastUpdateTime && Date.now() - lastUpdateTime.getTime() > 10000) {\n"
    "                isOnline = false;\n"
    "                updateStatus(0);\n"
    "            }\n"
    "        }, 10000);\n"
    "    </script>\n"
    "</body>\n"
    "</html>";

    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init finished. Connecting to %s...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to WiFi");
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }
}

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };

        httpd_uri_t api_uri = {
            .uri = "/api/data",
            .method = HTTP_GET,
            .handler = api_data_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &api_uri);

        ESP_LOGI(TAG, "Web server started");
    }

    return server;
}

void sensor_task(void *pvParameter) {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);

    uint8_t data[BUF_SIZE];

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (len >= 32) {
            for (int i = 0; i < len - 32; i++) {
                if (data[i] == 0x42 && data[i+1] == 0x4D) {
                    parse_pms_frame(&data[i]);
                    i += 31;
                }
            }
        } else {
            ESP_LOGW(TAG, "No complete frame received");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_sta();

    start_webserver();

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}
