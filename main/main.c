#include "data.h"
#include "log.h"
#include "wifi.h"
#include "alarm.h"

void app_main(void)
{
    struct data data = { 0 };

    log_fatal(data_read(&data));

    log_fatal(wifi_driver_init());

    for (;;) {
        if (*data.ssid) {
            const char *err;
            if ((err = wifi_sta_init(&data))) {
                log_error(err);
            } else {
                break;
            }
        }
        log_fatal(wifi_ap_init(&data));
    }
    // TODO: try to sync NTP once a day
    log_fatal(alarm_start(&data));
}
