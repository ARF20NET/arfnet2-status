#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "monitor.h"

typedef enum {
    TYPE_REACH,
    TYPE_DNS,
    TYPE_WEB
} type_t;

const char *type_str[] = { "reach", "dns", "web" };

typedef enum {
    STATUS_DOWN,
    STATUS_UP
} status_t;

typedef struct {
    type_t type;
    char *target;
    status_t status;
} target_t;

static target_t targets[256];
static size_t target_n = 0;

int
monitor_init(const char *cfg_file) {
    FILE *cfgf = fopen(cfg_file, "r");
    if (!cfgf) {
        fprintf(stderr, "Error opening config: %s\n", strerror(errno));
        return -1;
    }

    printf("monitor targets:\n");
    
    char line[256];
    while (fgets(line, sizeof(line), cfgf)) {
        if (*line == '#' || *line == '\n')
            continue;

        char *type = line;
        char *target = strchr(line, '=');
        if (!target) {
            fprintf(stderr, "malformed config line: %s\n", line);
            continue;
        }


        *target = '\0';
        target++;

        if (strcmp(type, "reach") == 0)
            targets[target_n].type = TYPE_REACH;
        else if (strcmp(type, "dns") == 0)
            targets[target_n].type = TYPE_DNS;
        else if (strcmp(type, "web") == 0)
            targets[target_n].type = TYPE_WEB;

        targets[target_n].target = strdup(target);
        targets[target_n].status = STATUS_DOWN;

        printf("\t%s: %s", type_str[targets[target_n].type],
            targets[target_n].target);
        
        target_n++;
    } 

    fclose(cfgf);

    return 0;
}

const char *
monitor_generate_status_html()
{
    static char buff[65535];

    static char *status_html[] = {
        "down",
        "up"
    };
   
    char *pos = buff;

    for (size_t i = 0; i < target_n; i++) {
        pos += snprintf(pos, 65535,
            "<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n",
            type_str[targets[i].type],
            targets[i].target,
            status_html[targets[i].status]);
    }

    return buff;  
}

void
monitor_check()
{

}

