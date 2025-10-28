#ifndef _MONITOR_H
#define _MONITOR_H

#include <time.h>

typedef enum {
    TYPE_REACH,
    TYPE_DNS,
    TYPE_WEB
} type_t;

typedef enum {
    STATUS_DOWN,
    STATUS_UP
} status_t;

typedef struct {
    time_t time;
    status_t status;
} event_t;

typedef struct {
    type_t type;
    char *name;
    char *target;

    status_t status, status_1;

    event_t *events;
    size_t events_size, events_capacity;
} target_t;


extern target_t *targets;
extern size_t targets_size;

int monitor_init();
const char *monitor_generate_status_html();
const char *monitor_generate_incidents_html();
void monitor_check();
void monitor_update_events(const char *log_path);

#endif /* _MONITOR_H */
