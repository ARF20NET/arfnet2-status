#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

unsigned short port = DEFAULT_PORT;
char *log_path = DEFAULT_LOG_PATH;
monitor_config_t monitor_config = { .interval = DEFAULT_INTERVAL };
alert_config_t   alert_config = { 0 };

int
config_load(const char *conf_path)
{
    FILE *cfgf = fopen(conf_path, "r");
    if (!cfgf) {
        fprintf(stderr, "Error opening config: %s\n", strerror(errno));
        return -1;
    }

    fseek(cfgf , 0, SEEK_END);
    size_t cfgsize = ftell(cfgf);
    rewind(cfgf);

    monitor_config.target_config = malloc(cfgsize);
    alert_config.alert_config = malloc(cfgsize);

    char *target_pos = monitor_config.target_config;
    char *alert_pos = alert_config.alert_config;

    printf("config:\n");
    
    char line[256];
    while (fgets(line, sizeof(line), cfgf)) {
        if (*line == '#' || *line == '\n')
            continue;

        char *separator = strchr(line, '=');
        if (!separator) {
            fprintf(stderr, "[config] malformed line: %s\n", line);
            continue;
        }

        *separator = '\0';

        char *value = separator + 1;

        if (strcmp(line, "port") == 0) {
            port = atoi(value);
            printf("\tport: %d\n", port);
            if (port == 0) {
                fprintf(stderr, "[config] invalid port: %s\n", line);
                return -1;
            }
        } else if (strcmp(line, "interval") == 0) {
            monitor_config.interval = atoi(value);
            printf("\tinterval: %ld\n", monitor_config.interval);
            if (monitor_config.interval == 0) {
                fprintf(stderr, "[config] invalid interval: %s\n", line);
                return -1;
            }
        } else if (strcmp(line, "log") == 0) {
            value[strlen(value) - 1] = '\0';
            log_path = strdup(value);
            printf("\tlog path: %s\n", log_path);
        } else if (strcmp(line, "mailserver") == 0) {
            value[strlen(value) - 1] = '\0';
            alert_config.mail_server = strdup(value);
            printf("\tmailserver: %s\n", alert_config.mail_server);
        } else if (strcmp(line, "mailfrom") == 0) {
            value[strlen(value) - 1] = '\0';
            alert_config.from = strdup(value);
            printf("\tfrom: %s\n", log_path);
        } else if (strcmp(line, "mailuser") == 0) {
            value[strlen(value) - 1] = '\0';
            alert_config.user = strdup(value);
        } else if (strcmp(line, "mailpassword") == 0) {
            value[strlen(value) - 1] = '\0';
            alert_config.password = strdup(value);
        }

        else if (strcmp(line, "target") == 0) {
            target_pos += snprintf(target_pos,
                cfgsize - (target_pos - monitor_config.target_config),
                "%s", value);
        } else if (strcmp(line, "alert") == 0) {
            alert_pos += snprintf(alert_pos,
                cfgsize - (alert_pos - alert_config.alert_config),
                "%s", value);
        } else {
            fprintf(stderr, "[config] unknown key: %s\n", line);
            continue;
        }

    }

    fclose(cfgf);

    if (!alert_config.from || !alert_config.mail_server)
        fprintf(stderr, "[config] W: no mail\n");

    return 0;
}

