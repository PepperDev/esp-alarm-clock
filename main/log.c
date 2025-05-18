#include "log.h"

#include "esp_log.h"

static const char *TAG = "alarm";       // pcTaskGetName(NULL)

void log_info(const char *message)
{
    ESP_LOGI(TAG, "%s", message);
}

void log_error(const char *message)
{
    ESP_LOGE(TAG, "%s", message);
}

void log_fatal(const char *message)
{
    if (!message) {
        return;
    }
    ESP_LOGE(TAG, "%s", message);
    abort();
}
