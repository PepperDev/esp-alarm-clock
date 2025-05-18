#include "alarm.h"
#include "data.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "cJSON.h"

#include "esp_http_server.h"
#include "esp_event.h"

extern const char home_start[] asm("_binary_alarm_html_start");
extern const char home_end[] asm("_binary_alarm_html_end");

static struct {
    struct data *data;
    unsigned char color;
    SemaphoreHandle_t signal;
} __attribute__((packed)) context;

static esp_err_t route_home_handler(httpd_req_t *);
static esp_err_t route_post_handler(httpd_req_t *);
static esp_err_t http_404_error_handler(httpd_req_t *, httpd_err_code_t);

const char *alarm_start(struct data *data)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 4000,        // 4 kHz
        .clk_cfg = LEDC_AUTO_CLK
    };
    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        return "Unable to setup LED timer.";
    }
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = 0,
        .duty = 0,              // Set duty to 0%
        .hpoint = 0
    };
    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        return "Unable to setup LED 0 channel.";
    }
    ledc_channel.channel = LEDC_CHANNEL_1;
    ledc_channel.gpio_num = 1;
    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        return "Unable to setup LED 1 channel.";
    }
    ledc_channel.channel = LEDC_CHANNEL_2;
    ledc_channel.gpio_num = 2;
    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        return "Unable to setup LED 2 channel.";
    }
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) != ESP_OK) {
        return "Unable to start alarm http server.";
    }
    const httpd_uri_t route_home = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = route_home_handler
    };
    if (httpd_register_uri_handler(server, &route_home) != ESP_OK) {
        return "Unable to register alarm http home route.";
    }
    context.data = data;
    // context.signal = *(SemaphoreHandle_t *) signal;
    const httpd_uri_t route_action = {
        .uri = "/",
        .method = HTTP_POST,
        .handler = route_post_handler,
    };
    if (httpd_register_uri_handler(server, &route_action) != ESP_OK) {
        return "Unable to register alarm http action route.";
    }
    if (httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler) != ESP_OK) {
        return "Unable to register setup http error handler.";
    }
    for (;;) {
        // estimate wait period for the next alarm action, but allow interruption with signal in case of change
        vTaskDelay(pdMS_TO_TICKS(5000));
        //if (xSemaphoreTake(signal, pdMS_TO_TICKS(30000)) != pdTRUE) {
        // ...
        //}
        // ...
    }
    return NULL;
}

static esp_err_t route_home_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, home_start, home_end - home_start);
    return ESP_OK;
}

static esp_err_t route_post_handler(httpd_req_t *req)
{
    size_t total = req->content_len;
    if (total > 4096) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    char *buffer = malloc(total + 1);
    if (!buffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }
    size_t cur_len = 0;
    while (cur_len < total) {
        size_t received = httpd_req_recv(req, buffer + cur_len, total - cur_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            free(buffer);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buffer[total] = 0;
    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse json");
        free(buffer);
        return ESP_FAIL;
    }
    cJSON *node = cJSON_GetObjectItem(root, "color");
    if (node) {
        int color = abs(node->valueint) % 256;
        int bright = 255;
        if ((node = cJSON_GetObjectItem(root, "brightness"))) {
            bright = abs(node->valueint) % 256;
        }
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (color % 6) * (255 / 5) * bright / 255);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, ((color / 6) % 6) * (255 / 5) * bright / 255);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, ((color / (6 * 6)) % 6) * (255 / 5) * bright / 255);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
        httpd_resp_sendstr(req, "color changed");
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Page not found");
    return ESP_FAIL;
}
