#ifndef _DATA_H
#define _DATA_H

struct alarm {
    char hour;                  // default 7
    char minute;                // default 0
    unsigned char repeat;       // enabled bit, default 0xFF
    // char sound;
    unsigned char volume;       // default 96
    unsigned char sunrise_time; // minutes, default 5
    unsigned char sunrise_brightness;   // default 255
    unsigned char ring_time;    // minutes, default 30
    unsigned char sleep_time;   // multiple 5 minutes, default 41 = 8h05
    unsigned char sleep_aid_time;       // minutes, default 15
    unsigned char sleep_aid_brightness; // default 64
    unsigned char sleep_aid_fade;       // multiple 5 seconds, default 24 = 2 minutes
    unsigned char sleep_aid_colour;     // 2 bit rgb, default: 0xB4 = red
    unsigned char pre_sleep_aid_time;   // minutes, default 120
    unsigned char pre_sleep_aid_brightness;     // default 255
    unsigned char pre_sleep_aid_fade;   // multiple 5 seconds, default 60 = 5 minutes
    unsigned char pre_sleep_aid_colour; // 2 bit rgb, default: 0x05 = blue
} __attribute__((packed));

struct data {
    char ssid[33];
    char password[64];
    char timezone[64];
    struct alarm alarm[5];
} __attribute__((packed));

const char *data_read(struct data *);
const char *data_write(struct data *);

#endif
