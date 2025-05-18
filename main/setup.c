#include "setup.h"
#include "data.h"
#include "form.h"

#include "esp_http_server.h"
#include "esp_event.h"

extern const char setup_start[] asm("_binary_setup_html_start");
extern const char setup_end[] asm("_binary_setup_html_end");

static char *location;
static httpd_handle_t server;
static struct {
    struct data *data;
    SemaphoreHandle_t signal;
} __attribute__((packed)) context;

static esp_err_t route_home_handler(httpd_req_t *);
static esp_err_t route_setup_handler(httpd_req_t *);
static esp_err_t http_404_error_handler(httpd_req_t *, httpd_err_code_t);

const char *setup_server(char *uri, struct data *data, void *signal)
{
    server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) != ESP_OK) {
        return "Unable to start setup http server.";
    }
    location = uri;
    const httpd_uri_t route_home = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = route_home_handler
    };
    if (httpd_register_uri_handler(server, &route_home) != ESP_OK) {
        return "Unable to register setup http home route.";
    }
    context.data = data;
    context.signal = *(SemaphoreHandle_t *) signal;
    const httpd_uri_t route_action = {
        .uri = "/",
        .method = HTTP_POST,
        .handler = route_setup_handler,
    };
    if (httpd_register_uri_handler(server, &route_action) != ESP_OK) {
        return "Unable to register setup http action route.";
    }
    if (httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler) != ESP_OK) {
        return "Unable to register setup http error handler.";
    }
    return NULL;
}

void setup_stop_server()
{
    httpd_stop(server);
}

static esp_err_t route_home_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, setup_start, setup_end - setup_start);
    return ESP_OK;
}

static esp_err_t route_setup_handler(httpd_req_t *req)
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
    struct form_data form_data[3] = {
        {
         .key = "ssid",
         .key_len = 4,
         .value = context.data->ssid,
         .value_len = sizeof(((struct data *) NULL)->ssid)
         },
        {
         .key = "password",
         .key_len = 8,
         .value = context.data->password,
         .value_len = sizeof(((struct data *) NULL)->password)
         },
        {
         .key = "timezone",
         .key_len = 8,
         .value = context.data->timezone,
         .value_len = sizeof(((struct data *) NULL)->timezone)
         }
    };
    form_parse(buffer, total, form_data, 3);
    free(buffer);
    httpd_resp_sendstr(req, "Setup completed");
    xSemaphoreGive(context.signal);
    return ESP_OK;
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, "setup", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
