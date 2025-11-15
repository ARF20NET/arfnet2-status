#ifndef _CONFIG_H
#define _CONFIG_H

#include <time.h>


#define BUFF_SIZE           65535
#define INIT_VEC_CAPACITY   256
#define CONFIG_PATH         "monitor.cfg"

#define DEFAULT_PORT        8888
#define DEFAULT_INTERVAL    60
#define DEFAULT_SAMPLES     60
#define DEFAULT_TMPL_PATH   "index.htm.tmpl"
#define DEFAULT_LOG_PATH    "events.log"

/* config types */
typedef struct {
    time_t interval;
    int samples;
    char *target_config;
} monitor_config_t;

typedef struct {
    char *mail_server;
    char *from;
    char *user;
    char *password;
    char *alert_config;
} alert_config_t;


/* config objects */
extern unsigned short port;
extern char *tmpl_path;
extern char *log_path;
extern monitor_config_t monitor_config;
extern alert_config_t   alert_config;


int config_load(const char *conf_path);

#endif /* _CONFIG_H */

