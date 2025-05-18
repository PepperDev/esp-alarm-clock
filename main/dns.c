#include "dns.h"

#include "lwip/sockets.h"
#include "esp_event.h"

static TaskHandle_t task = { 0 };

static int sock = -1;

struct dns_header {
    short unsigned int id;
    short unsigned int flags;
    short unsigned int queries;
    short unsigned int answers;
    short unsigned int records;
    short unsigned int extra_records;
} __attribute__((packed));

static void dns_task(void *);
static size_t dns_len(const char *, size_t);

const char *dns_start(long unsigned int *ip_addr)
{
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return "Unable to create dns socket.";
    }
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr = (struct in_addr) {0},
        .sin_port = 0x3500,
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr))) {
        close(sock);
        return "Unable to associate UDP port.";
    }
    if (xTaskCreate(dns_task, "dns", 2048, ip_addr, 5, &task) != pdPASS) {
        shutdown(sock, 0);
        close(sock);
        return "Unable to create dns daemon.";
    }
    return NULL;
}

void dns_stop()
{
    shutdown(sock, 0);
    close(sock);
    vTaskDelete(task);
}

static void dns_task(void *param)
{
    char buffer[512];
    for (;;) {
        struct sockaddr_in6 source;
        socklen_t size = sizeof(source);
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&source, &size);
        if (len < 0) {
            break;
        }
        if (len < sizeof(struct dns_header)) {
            continue;
        }
        // OPCODE = QUERY = 0
        if (((struct dns_header *)buffer)->flags & 0x5800) {
            continue;
        }
        ((struct dns_header *)buffer)->flags = (((struct dns_header *)buffer)->flags & ~0x2000) | 0x8080;       // Query Reply
        short unsigned int count = ntohs(((struct dns_header *)buffer)->queries);
        char *pos = buffer + sizeof(struct dns_header);
        size_t left = len - sizeof(struct dns_header);
        char *reply = buffer + len;
        size_t reply_len = len;
        short unsigned int answers = 0;
        bool ok = true;
        for (int i = 0; i < count; i++) {
            const char *offset_current = pos;
            size_t name_len;
            if ((((unsigned char)*pos) & 0xC0) == 0xC0) {
                short unsigned int offset = ntohs(*((short unsigned int *)pos) ^ 0xC000);
                if (offset >= len) {
                    ok = false;
                    break;
                }
                offset_current = buffer + offset;
                name_len = dns_len(offset_current, len - offset);
            } else {
                name_len = dns_len(pos, left);
            }
            if (name_len > 0xFF || left < name_len + 4) {
                ok = false;
                break;
            }
            // type A
            if (*(short unsigned int *)(pos + name_len) == 0x0100) {
                size_t reply_size = 12 + sizeof(struct in_addr);
                if (sizeof(buffer) - reply_len > reply_size) {
                    *(short unsigned int *)reply = htons(0xC000 | (offset_current - buffer));
                    reply += 2;
                    memcpy(reply, pos + name_len, 4);   // type and class
                    reply += 4;
                    *(long unsigned int *)reply = 0;    // TTL
                    reply += 4;
                    *(short unsigned int *)reply = 0x0400;      // htons(sizeof(struct in_addr));
                    reply += 2;
                    *(struct in_addr *)reply = *(struct in_addr *)param;
                    reply += sizeof(struct in_addr);
                    reply_len += reply_size;
                    answers++;
                }
            }
            pos += name_len + 4;
            left -= name_len + 4;
        }
        if (!ok) {
            continue;
        }
        if (left > 0) {
            memmove(pos, buffer + len, reply_len - len);
            reply_len -= left;
        }
        ((struct dns_header *)buffer)->records = 0;
        ((struct dns_header *)buffer)->extra_records = 0;
        ((struct dns_header *)buffer)->answers = htons(answers);
        sendto(sock, buffer, reply_len, 0, (struct sockaddr *)&source, sizeof(source));
    }
    shutdown(sock, 0);
    close(sock);
    vTaskDelete(NULL);
}

static size_t dns_len(const char *str, size_t max)
{
    char *pos = (char *)str;
    while (max > 0) {
        unsigned char len = *pos;
        if (len == 0) {
            pos++;
            break;
        }
        if (len > 63 || len > max - 1) {
            return 0x100;
        }
        len++;
        max -= len;
        pos += len;
    }
    return pos - str;
}
