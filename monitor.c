#define _GNU_SOURCE /* forgive me */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <curl/curl.h>

#include "monitor.h"

#define BUFF_SIZE           65535
#define INIT_VEC_CAPACITY   256

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
    time_t time;
    status_t status;
} event_t;

typedef struct {
    type_t type;
    char *name;
    char *target;

    status_t status;

    event_t *events;
    size_t event_size, event_capacity;
} target_t;


static target_t targets[INIT_VEC_CAPACITY];
static size_t target_n = 0;


static char timestr[256];



static void
target_events_push(target_t *target, event_t event)
{
    if (target->event_size + 1 > target->event_capacity)
        target->events = realloc(target->events, 2 * target->event_capacity);

    target->events[target->event_size++] = event;
}

static size_t
target_events_load(target_t *target, const char *logbuff) {
    char line[256];
    size_t n = 0;

    while (*logbuff) {
        char *nlpos = strchr(logbuff, '\n');
        if (!nlpos)
            return n;

        size_t linelen = nlpos - logbuff;
        strncpy(line, logbuff, linelen);
        line[linelen] = '\0';
        logbuff += linelen + 1;

        /* process line */
        if (line[0] == '\0' || line[0] == '#')
            continue;

        char *name = strtok(line, ",");
        char *time = strtok(NULL, ",");
        char *status = strtok(NULL, ",");

        if (!name || !time || !status) {
            fprintf(stderr, "malformed log line: %s\n", line);
            continue;
        }

        if (strcmp(name, target->name) != 0)
            continue;

        struct tm event_time;
        strptime(time, "%Y-%m-%d %H-%M-%S", &event_time);

        event_t event = {
            mktime(&event_time),
            strcmp(status, "up") == 0 ? STATUS_UP : STATUS_DOWN
        };

        target_events_push(target, event);

        n++;
    }

    return n;
}

int
monitor_init(const char *cfg_path, const char *log_path) {
    /* read monitor log */
    FILE *logf = fopen(log_path, "r");
    if (!logf) {
        fprintf(stderr, "Error opening log: %s\n", strerror(errno));
        return -1;
    }

    fseek(logf, 0, SEEK_END);
    size_t logsize = ftell(logf);
    rewind(logf);

    char *logbuff = malloc(logsize + 1);
    size_t logread = fread(logbuff, 1, logsize, logf);
    fclose(logf);

    logbuff[logread] = '\0';


    /* read monitoring configuration */
    FILE *cfgf = fopen(cfg_path, "r");
    if (!cfgf) {
        fprintf(stderr, "Error opening config: %s\n", strerror(errno));
        return -1;
    }

    printf("monitor targets:\n");
    
    char line[256];
    while (fgets(line, sizeof(line), cfgf)) {
        if (*line == '#' || *line == '\n')
            continue;

        line[strlen(line) - 1] = '\0'; /* strip \n */

        char *type = strtok(line, ",");
        char *name = strtok(NULL, ",");
        char *target = strtok(NULL, ",");

        if (!target || !name || !target) {
            fprintf(stderr, "malformed config line: %s\n", line);
            continue;
        }

        if (strcmp(type, "reach") == 0)
            targets[target_n].type = TYPE_REACH;
        else if (strcmp(type, "dns") == 0)
            targets[target_n].type = TYPE_DNS;
        else if (strcmp(type, "web") == 0)
            targets[target_n].type = TYPE_WEB;

        targets[target_n].name = strdup(name);
        targets[target_n].target = strdup(target);
        targets[target_n].status = STATUS_DOWN;

        /* read monitor logs */
        targets[target_n].event_capacity = INIT_VEC_CAPACITY;
        targets[target_n].event_size = 0;
        targets[target_n].events = malloc(INIT_VEC_CAPACITY * sizeof(event_t));

        size_t event_n = target_events_load(&targets[target_n], logbuff);

        printf("\t%s: %s,%s %ld events\n",
            targets[target_n].name,
            type_str[targets[target_n].type],
            targets[target_n].target,
            event_n
        );
        
        target_n++;
    } 

    fclose(cfgf);

    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res) {
        fprintf(stderr, "Error initializing cURL: %s\n",
            curl_easy_strerror(res));
        return -1;
    }

        return 0;
}

const char *
monitor_generate_status_html()
{
    static char buff[BUFF_SIZE];

    static char *status_html[] = {
        "<td class=\"down\">down</td>",
        "<td class=\"up\">up</td>"
    };
   
    char *pos = buff;

    for (size_t i = 0; i < target_n; i++) {
        pos += snprintf(pos, BUFF_SIZE,
            "<tr><td>%s</td><td>%s</td>%s<td></td><td>%s</td><td>%s</td>%s</tr>\n",
            type_str[targets[i].type],
            targets[i].target,
            status_html[targets[i].status],
            "",
            "",
            ""
        );
    }

    return buff;  
}


const char *
monitor_generate_incidents_html()
{
    static char buff[BUFF_SIZE];
   
    snprintf(buff, BUFF_SIZE, "<tr><td></td><td></td><td></td></tr>");

    return buff;
}

static int
check_reach(const char *target)
{
    static char ping_cmd[256];
    
    snprintf(ping_cmd, 256, "ping -W1 -c1 %s > /dev/null", target);
    /* i know */
    if (system(ping_cmd) == 0) {
        printf("reachable\n");
        return STATUS_UP;
    } else {
        printf("unreachable\n");
        return STATUS_DOWN;
    }
}

static int
check_dns(const char *name)
{
    static char dig_cmd[512];
    static char cmd_out[256];

    snprintf(dig_cmd, 512, "dig +nocookie +short %s NS", name);
    FILE *pf = popen(dig_cmd, "r");
    fread(cmd_out, 256, 1, pf);
    pclose(pf);

    if (*cmd_out == '\0') {
        printf("no ns\n");
        return STATUS_DOWN;
    }
    
    *strchr(cmd_out, '\n') = '\0';

    snprintf(dig_cmd, 512, "dig +nocookie +short @%s %s A", cmd_out, name);
    pf = popen(dig_cmd, "r");
    fread(cmd_out, 256, 1, pf);
    pclose(pf);
    
    if (*cmd_out == '\0') {
        printf("no a\n");
        return STATUS_DOWN;
    }
    
    *strchr(cmd_out, '\n') = '\0';

    printf("%s\n", cmd_out);

    return STATUS_UP;
}

static size_t
write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    return size * nmemb;
}

static int
check_http(const char *endpoint)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error allocating cURL handle\n");
        return -1;
    }

    //curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1);

    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    CURLcode curl_code = curl_easy_perform(curl);
    if (curl_code != CURLE_OK) {
        printf("curl_easy_perform() failed: %s\n",
            curl_easy_strerror(curl_code));
        return STATUS_DOWN;
    }

    long http_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    printf("%ld\n", http_code);

    return http_code == 200 ? STATUS_UP : STATUS_DOWN;
}

void
monitor_check()
{
    static size_t check_num = 0;
    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    strftime(timestr, 256, "%Y-%m-%d %H-%M-%S", tm_now);

    static const int (*check_funcs[])(const char *) = {
        check_reach,
        check_dns,
        check_http
    };

    for (size_t i = 0; i < target_n; i++) {
        printf("[%s] [monitor] check #%ld %s: ",
            timestr, check_num, targets[i].name);
        targets[i].status = check_funcs[targets[i].type](targets[i].target);
    }

    check_num++;
}

