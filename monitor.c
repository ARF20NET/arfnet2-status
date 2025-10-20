#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <curl/curl.h>

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

static CURL *curl;

static char timestr[256];

static size_t
write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    return size * nmemb;
}

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

        line[strlen(line) - 1] = '\0'; /* strip \n */

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

        printf("\t%s: %s\n", type_str[targets[target_n].type],
            targets[target_n].target);
        
        target_n++;
    } 

    fclose(cfgf);

    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res) {
        fprintf(stderr, "Error initializing cURL: %s\n",
            curl_easy_strerror(res));
        return -1;
    }

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error allocating cURL handle\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

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

static int
check_reach(const char *endpoint)
{
    return STATUS_DOWN;
}

static int
check_dns(const char *endpoint)
{
    return STATUS_DOWN;
}

static int
check_http(const char *endpoint)
{
    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    CURLcode curl_code = curl_easy_perform(curl);
    if (curl_code != CURLE_OK) {
        printf("[%s] [monitor] check http: %s: curl_easy_perform() failed: %s\n",
            timestr, endpoint, curl_easy_strerror(curl_code));
        return STATUS_DOWN;
    }

    long http_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    printf("[%s] [monitor] check http: %s: %ld\n",
        timestr, endpoint, http_code);

    return STATUS_UP;
}

void
monitor_check()
{
    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    strftime(timestr, 256, "%Y-%m-%d %H-%M-%S", tm_now);

    static const int (*check_funcs[])(const char *) = {
        check_reach,
        check_dns,
        check_http
    };

    for (size_t i = 0; i < target_n; i++)
        targets[i].status = check_funcs[targets[i].type](targets[i].target);
}

