#include <time.h>

#include "data.h"

#include "nvs_flash.h"

static const char namespace[] = "storage";
static const char key[] = "data";

const char *data_read(struct data *data)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            return "Unable to erase storage.";
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return "Unable to init storage.";
    }
    nvs_handle_t handle;
    if (nvs_open(namespace, NVS_READONLY, &handle) != ESP_OK) {
        return NULL;
    }

    size_t data_size = sizeof(struct data);
    err = nvs_get_blob(handle, key, data, &data_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return "Unable to read storage.";
    }
    nvs_close(handle);
    if (*data->timezone) {
        setenv("TZ", data->timezone, 1);
        tzset();
    }
    return NULL;
}

const char *data_write(struct data *data)
{
    nvs_handle_t handle;
    if (nvs_open(namespace, NVS_READWRITE, &handle) != ESP_OK) {
        return "Unable to open storage.";
    }
    if (nvs_set_blob(handle, key, data, sizeof(struct data)) != ESP_OK) {
        return "Unable to write storage.";
    }
    if (nvs_commit(handle) != ESP_OK) {
        return "Unable to persist storage.";
    }
    nvs_close(handle);
    return NULL;
}
