#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#define WIFI_SSID      "******"
#define WIFI_PASS      "******"

#define R_ENABLE_PIN   GPIO_NUM_0
#define L_ENABLE_PIN   GPIO_NUM_1
#define R_PWM_PIN      GPIO_NUM_2
#define L_PWM_PIN      GPIO_NUM_3

#define BLINK_GPIO GPIO_NUM_8
#define CONFIG_BLINK_LED_STRIP 1
#define CONFIG_BLINK_LED_STRIP_BACKEND_SPI 1

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_R          LEDC_CHANNEL_0
#define LEDC_CHANNEL_L          LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // Set duty resolutionn to 10 bits
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz

static const char *TAG = "MOTOR_CONTROL";

const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 Motor Control</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: Arial, Helvetica, sans-serif; }
  .card {
            max-width: 400px;
            margin: 0 auto;
            padding: 20px;
            border: 1px solid #ccc;
            border-radius: 5px;
        }
  .slider {
            -webkit-appearance: none;
            width: 100%;
            height: 15px;
            border-radius: 5px;
            background: #d3d3d3;
            outline: none;
            opacity: 0.7;
            -webkit-transition: .2s;
            transition: opacity .2s;
        }
  .slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 25px;
            height: 25px;
            border-radius: 50%;
            background: #4CAF50;
            cursor: pointer;
        }
  .slider::-moz-range-thumb {
            width: 25px;
            height: 25px;
            border-radius: 50%;
            background: #4CAF50;
            cursor: pointer;
        }
  .button {
            background-color: #4CAF50;
            border: none;
            color: white;
            padding: 15px 32px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 16px;
            margin: 4px 2px;
            cursor: pointer;
            border-radius: 5px;
        }
  .button-stop {
            background-color: #f44336;
        }
</style>
<script>
function sendRequest(url) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", url, true);
  xhr.send();
}

function updateSpeed(value) {
  document.getElementById("speedValue").innerHTML = value;
  sendRequest("/motor/speed?value=" + value);
}

function goForward() {
  sendRequest("/motor/direction?dir=forward");
}

function goBackward() {
  sendRequest("/motor/direction?dir=backward");
}

function stopMotor() {
  sendRequest("/motor/direction?dir=stop");
}
</script>
</head>
<body>

<div class="card">
  <h2>ESP32 Motor Control</h2>
  <p>Speed: <span id="speedValue">0</span></p>
  <input type="range" min="0" max="1023" value="0" class="slider" id="speedSlider" onchange="updateSpeed(this.value)">
  
  <p>
    <button class="button" onclick="goForward()">Forward</button>
    <button class="button" onclick="goBackward()">Backward</button>
    <button class="button button-stop" onclick="stopMotor()">Stop</button>
  </p>
</div>

</body>
</html>
)rawliteral";

void led_init() {
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

void led_indicate_request() {
    ESP_LOGI(TAG, "Indicating request");
    gpio_set_level(BLINK_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(BLINK_GPIO, 0);
}

void motor_init() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel_r = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_R,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = R_PWM_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_r));

    ledc_channel_config_t ledc_channel_l = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_L,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = L_PWM_PIN,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_l));

    gpio_reset_pin(R_ENABLE_PIN);
    gpio_set_direction(R_ENABLE_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(L_ENABLE_PIN);
    gpio_set_direction(L_ENABLE_PIN, GPIO_MODE_OUTPUT);
}

void motor_set_speed(uint32_t speed) {
    if (speed > 1023) speed = 1023;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, speed));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_L, speed));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_L));
}

void motor_set_direction(const char* dir) {
    if (strcmp(dir, "forward") == 0) {
        gpio_set_level(R_ENABLE_PIN, 1);
        gpio_set_level(L_ENABLE_PIN, 0);
    } else if (strcmp(dir, "backward") == 0) {
        gpio_set_level(R_ENABLE_PIN, 0);
        gpio_set_level(L_ENABLE_PIN, 1);
    } else { // stop
        gpio_set_level(R_ENABLE_PIN, 0);
        gpio_set_level(L_ENABLE_PIN, 0);
    }
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

static esp_err_t speed_get_handler(httpd_req_t *req) {
    char*  buf;
    size_t buf_len;

    ESP_LOGI(TAG, "speed_get_handler called");
    led_indicate_request();

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
                int speed = atoi(param);
                motor_set_speed(speed);
            }
        }
        free(buf);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t direction_get_handler(httpd_req_t *req) {
    char*  buf;
    size_t buf_len;

    ESP_LOGI(TAG, "direction_get_handler called");
    led_indicate_request();

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "dir", param, sizeof(param)) == ESP_OK) {
                motor_set_direction(param);
            }
        }
        free(buf);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t motor_speed = {
    .uri       = "/motor/speed",
    .method    = HTTP_GET,
    .handler   = speed_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t motor_direction = {
    .uri       = "/motor/direction",
    .method    = HTTP_GET,
    .handler   = direction_get_handler,
    .user_ctx  = NULL
};

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &motor_speed);
        httpd_register_uri_handler(server, &motor_direction);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
    }
}

static void wifi_event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
)

{
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "connect to the AP fail");
        esp_wifi_connect();
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        &instance_any_id
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        &instance_got_ip
    ));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_init();
    motor_init();
    wifi_init_sta();
}