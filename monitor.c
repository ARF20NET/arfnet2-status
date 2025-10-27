#define _GNU_SOURCE /* forgive me */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

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

    status_t status, status_1;

    event_t *events;
    size_t events_size, events_capacity;
} target_t;

/* baked */
typedef struct {
    time_t started_time;
    time_t duration_time;
    int resolved;
    char *service;
    char *started;
    char *duration;
} incident_t;


static target_t targets[INIT_VEC_CAPACITY];
static size_t targets_n = 0;

/* ordered*/
static incident_t *incidents = NULL;
static size_t incidents_size = 0, incidents_capacity = 0;

static char timestr[256];


static void
target_events_push_ordered(target_t *target, const event_t *event)
{
    if (target->events_size + 1 > target->events_capacity)
        target->events = realloc(target->events,
            2 * sizeof(event_t) * target->events_capacity);

    size_t i = 0;
    while (target->events[i].time < event->time
            && i < target->events_size)
        i++;
    /* incidents[i].started_time >= incident.started_time */
    memmove(&target->events[i + 1], &target->events[i],
        (target->events_size - i) * sizeof(event_t));

    target->events[i] = *event;

    target->events_size++;
}

static void
incidents_push_ordered(const incident_t *incident)
{
    if (incidents_size + 1 > incidents_capacity)
        incidents = realloc(incidents,
            2 * sizeof(incident_t) * incidents_capacity);

    size_t i = 0;
    while (incidents[i].started_time < incident->started_time
            && i < incidents_size)
        i++;
    /* incidents[i].started_time >= incident.started_time */
    memmove(&incidents[i + 1], &incidents[i],
        (incidents_size - i) * sizeof(incident_t));

    incidents[i] = *incident;

    incidents_size++;
}

static size_t
target_events_load(target_t *target, const char *logbuff)
{
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

        struct tm event_time = { 0 };
        strptime(time, "%FT%T%z", &event_time);

        event_t event = {
            mktime(&event_time) - timezone,
            strcmp(status, "up") == 0 ? STATUS_UP : STATUS_DOWN
        };

        target_events_push_ordered(target, &event);

        n++;
    }

    return n;
}

/* assume events start with down */
void
incidents_render()
{
    char buff[256];

    incidents_size = 0;

    for (size_t i = 0; i < targets_n; i++) {
        /* iterate through downs */
        for (size_t j = 0; j < targets[i].events_size; j++) {
            if (targets[i].events[j].status != STATUS_DOWN)
                continue;
            /* next must be up */
            int resolved = 1;
            if (targets[i].events_size == j + 1 ||
                    targets[i].events[j + 1].status != STATUS_UP)
                resolved = 0;

            time_t start = targets[i].events[j].time;
            time_t duration = resolved ?
                targets[i].events[j + 1].time - start :
                time(NULL) - start;

            snprintf(buff, 256, "%ldh %ldm %lds", duration / 3600,
                (duration / 60) % 60, duration % 60);
            char *durationstr = strdup(buff);

            struct tm *tm_start = gmtime(&start);
            strftime(buff, 256, "%Y-%m-%d %H:%M:%S", tm_start);
            char *startstr = strdup(buff);

            incident_t incident = {
                targets[i].events[j].time,
                duration,
                resolved,
                targets[i].name,
                startstr,
                durationstr
            };

            incidents_push_ordered(&incident);
        }
    }
}

int
monitor_init(const char *cfg_path, const char *log_path)
{
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

    tzset(); /* initialize tz conversion */
    
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
            targets[targets_n].type = TYPE_REACH;
        else if (strcmp(type, "dns") == 0)
            targets[targets_n].type = TYPE_DNS;
        else if (strcmp(type, "web") == 0)
            targets[targets_n].type = TYPE_WEB;

        targets[targets_n].name = strdup(name);
        targets[targets_n].target = strdup(target);
        targets[targets_n].status = STATUS_DOWN;

        /* read monitor logs */
        targets[targets_n].events_capacity = INIT_VEC_CAPACITY;
        targets[targets_n].events_size = 0;
        targets[targets_n].events = malloc(INIT_VEC_CAPACITY * sizeof(event_t));

        size_t event_n = target_events_load(&targets[targets_n], logbuff);

        printf("\t%s: %s,%s %ld events\n",
            targets[targets_n].name,
            type_str[targets[targets_n].type],
            targets[targets_n].target,
            event_n
        );
        
        targets_n++;
    } 

    fclose(cfgf);

    incidents = malloc(sizeof(incident_t) * INIT_VEC_CAPACITY);
    incidents_capacity = INIT_VEC_CAPACITY;
    incidents_size = 0;

    incidents_render();

    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res) {
        fprintf(stderr, "Error initializing cURL: %s\n",
            curl_easy_strerror(res));
        return -1;
    }

    return 0;
}

const char *
target_uptime(const target_t *target)
{
    static char buff[256];

    if (target->events_size == 0) {
        snprintf(buff, 256, "-");
        return buff;
    }

    event_t last_event = target->events[target->events_size - 1];

    if (last_event.status == STATUS_DOWN) {
        snprintf(buff, 256, "-");
        return buff;
    }

    time_t uptime = time(NULL) - last_event.time;
    snprintf(buff, 256, "%ldd %ldh %ldm %lds",
        uptime / (3600*24), (uptime / 3600) % 24, (uptime / 60) % 60, uptime % 60);

    return buff;
}

float
target_perc_uptime_since(const target_t *target, time_t since)
{
    time_t downtime = 0;

    if (since < target->events[0].time)
        downtime += target->events[0].time - since;

    for (size_t i = 0; i < incidents_size; i++) {
        if (strcmp(incidents[i].service, target->name) != 0)
            continue;

        if (incidents[i].started_time + incidents[i].duration_time < since)
            continue;

        time_t clamped_duration = incidents[i].duration_time;
        if (incidents[i].started_time < since)
            clamped_duration -= since - incidents[i].started_time;

        downtime += clamped_duration;
    }

    return 1.0f - ((float)downtime / (float)(time(NULL) - since));
}

float
target_perc_uptime_total(const target_t *target)
{
    time_t downtime = 0, since = 0;

    for (size_t i = 0; i < incidents_size; i++) {
        if (strcmp(incidents[i].service, target->name) != 0)
            continue;

        if (since == 0)
            since = incidents[i].started_time;

        downtime += incidents[i].duration_time;
    }

    return 1.0f - ((float)downtime / (float)(time(NULL) - since));
}

int
color_map(float perc)
{
    return 255.0f*exp2f(100.0f*perc-100.0f);
}

const char *
generate_timeline(const target_t *target, time_t since, time_t span)
{
    static char buff[BUFF_SIZE];

    char *pos = buff;

    pos += snprintf(pos, BUFF_SIZE - (pos - buff),
        "<table class=\"graph\" style=\"width:100%%;\"><tr>");

    const incident_t *last_incident = NULL;

    for (size_t i = 0; i < incidents_size; i++) {
        if (strcmp(incidents[i].service, target->name) != 0)
            continue;

        if (incidents[i].started_time + incidents[i].duration_time < since)
            continue;

        time_t clamped_duration = incidents[i].duration_time;
        if (incidents[i].started_time < since)
            clamped_duration -= since - incidents[i].started_time;

        if (last_incident) {
            pos += snprintf(pos, BUFF_SIZE - (pos - buff),
                "<td class=\"up graph\" style=\"width:%2f%%;\"></td>",
                100.0f*((float)(incidents[i].started_time -
                    (last_incident->started_time + last_incident->duration_time)
                )/(float)span)
            );
        } else {
             pos += snprintf(pos, BUFF_SIZE - (pos - buff),
                "<td class=\"up graph\" style=\"width:%2f%%;\"></td>",
                100.0f*((float)(incidents[i].started_time - since)
                /(float)span)
            );
        }

        pos += snprintf(pos, BUFF_SIZE - (pos - buff),
            "<td class=\"down graph\" style=\"width:%2f%%;\"></td>",
                100.0f*((float)clamped_duration/(float)span)
            );

        last_incident = &incidents[i];
    }

    if (last_incident && last_incident->resolved) {
            pos += snprintf(pos, BUFF_SIZE - (pos - buff),
                "<td class=\"up graph\" style=\"width:%2f%%;\"></td>",
                100.0f*((float)(time(NULL) -
                    (last_incident->started_time + last_incident->duration_time)
                )/(float)span)
            );
        
    }

    pos += snprintf(pos, BUFF_SIZE - (pos - buff), "</tr></table>");

    return buff;
}

const char *
monitor_generate_status_html()
{
    static char buff[BUFF_SIZE];

    static char *status_html[] = {
        "<td class=\"w-min down\">down</td>",
        "<td class=\"w-min up\">up</td>"
    };
   
    char *pos = buff;

    for (size_t i = 0; i < targets_n; i++) {
        float perc_month = target_perc_uptime_since(&targets[i],
            time(NULL) - (30*24*3600));
        float perc_total = target_perc_uptime_total(&targets[i]);

        pos += snprintf(pos, BUFF_SIZE - (pos - buff),
            "<tr><td class=\"w-min\">%s</td>"
            "<td class=\"w-min\">%s</td>%s<td class=\"w-min\">%s</td>"
            "<td class=\"w-min\" style=\"background-color:rgb(%d,%d,0);\">%f</td>"
            "<td class=\"w-min\" style=\"background-color:rgb(%d,%d,0);\">%f</td>"
            "<td class=\"w-max\">%s</td></tr>\n",
            type_str[targets[i].type],          /* type */
            targets[i].target,                  /* target */
            status_html[targets[i].status],     /* status */
            target_uptime(&targets[i]),         /* uptime */
            255-color_map(perc_month), color_map(perc_month), 100.0f*perc_month,
            255-color_map(perc_total), color_map(perc_total), 100.0f*perc_total,
            generate_timeline(&targets[i], time(NULL) - 7*24*3600, 7*24*3600)
        );
    }

    return buff;  
}


const char *
monitor_generate_incidents_html()
{
    static char buff[BUFF_SIZE];

    static const char *resolvedstr[] = {
        "unresolved",
        "resolved"
    };
    
    char *pos = buff;
  
    for (int i = incidents_size - 1; i >= 0; i--) {
        pos += snprintf(pos, BUFF_SIZE - (pos - buff),
            "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>\n",
            incidents[i].service,
            resolvedstr[incidents[i].resolved],
            incidents[i].started,
            incidents[i].duration
        );
    }

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
    
    if (*cmd_out == '\0' || !isdigit(*cmd_out)) {
        printf("no a: %s\n", cmd_out);
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
    strftime(timestr, 256, "%Y-%m-%d %H:%M:%S", tm_now);

    static const int (*check_funcs[])(const char *) = {
        check_reach,
        check_dns,
        check_http
    };

    for (size_t i = 0; i < targets_n; i++) {
        printf("[%s] [monitor] check #%ld %s: ",
            timestr, check_num, targets[i].name);
        targets[i].status = check_funcs[targets[i].type](targets[i].target);
    }

    check_num++;
}

void
monitor_update_events()
{
    static char *status_str[] = {
        "down",
        "up"
    };

    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    strftime(timestr, 256, "%Y-%m-%d %H:%M:%S", tm_now);

    for (size_t i = 0; i < targets_n; i++) {
        if (targets[i].events_size > 0 && (
            targets[i].status ==
            targets[i].events[targets[i].events_size - 1].status))
        {
            continue;
        }

        if (targets[i].status != targets[i].status_1) {
            targets[i].status_1 = targets[i].status;
            continue;
        }

        event_t event = {
            time_now,
            targets[i].status
        };

        target_events_push_ordered(&targets[i], &event);

        printf("[%s] [monitor] %s is now %s\n",
            timestr, targets[i].name, status_str[targets[i].status]);

        incidents_render();
            
        targets[i].status_1 = targets[i].status;
    }
}

