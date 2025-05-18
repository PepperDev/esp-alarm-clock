#ifndef _SETUP_H
#define _SETUP_H

struct data;

const char *setup_server(char *, struct data *, void *signal);
void setup_stop_server();

#endif
