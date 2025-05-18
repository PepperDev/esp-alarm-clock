#ifndef _WIFI_H
#define _WIFI_H

struct data;

const char *wifi_driver_init();
const char *wifi_ap_init(struct data *);
const char *wifi_sta_init(struct data *);

#endif
