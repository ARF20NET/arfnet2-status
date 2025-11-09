#include "alert.h"

#include "config.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TYPE_API,
    TYPE_EMAIL
} alert_type_t;

typedef struct {
    alert_type_t type;
    char *target;   /* in api: endpoint,     in email: address */
    char *extra;    /* in api: Content-Type, in email: subject template */
    char *body_tmpl;
} alert_t;

alert_t *alerts = NULL;
size_t alerts_size = 0, alerts_capacity = INIT_VEC_CAPACITY;

static const char *type_str[] = { "api", "email" };
static const char *status_str[] = { "down", "up" };
static const char *from = NULL;


static size_t
write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
    return size * nmemb;
}

static int
send_api(const target_t *target, const char *endpoint, const char *content_type,
    const char *body_tmpl)
{
    static char buff[4096];

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error allocating cURL handle\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1);

    curl_easy_setopt(curl, CURLOPT_URL, endpoint);

    struct curl_slist *list = NULL;
    snprintf(buff, 256, "Content-Type: %s", content_type);
    list = curl_slist_append(list, buff); /* copies */
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

    snprintf(buff, 4096, body_tmpl, target->name, status_str[target->status]);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buff);

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


static int
send_email(const target_t *target, const char *address, const char *subject_tmpl,
    const char *body_tmpl)
{
    
}


int
alert_init()
{
    alerts = malloc(INIT_VEC_CAPACITY * sizeof(alert_t));

    from = alert_config.from;
    
    printf("alerts:\n");

    char line[256];
    char *src_line = alert_config.alert_config;
    while (src_line != (void*)1) {
        char *next_line = strchr(src_line, '\n');
        size_t linelen = next_line ? next_line - src_line : strlen(src_line);
        strncpy(line, src_line, linelen);
        line[linelen] = '\0';
        src_line = next_line + 1;

        if (*line == '\n' || *line == '\0')
            continue;

        char *type = strtok(line, ",");
        char *target = strtok(NULL, ",");
        char *extra = strtok(NULL, ",");
        char *body_tmpl = strtok(NULL, ",");

        if (!type || !target) {
            fprintf(stderr, "malformed config line: %s\n", line);
            continue;
        }

        if (strcmp(type, "api") == 0)
            alerts[alerts_size].type = TYPE_API;
        else if (strcmp(type, "email") == 0)
            alerts[alerts_size].type = TYPE_EMAIL;
        else {
            fprintf(stderr, "unknown alert type: %s\n", line);
            continue;
        }

        alerts[alerts_size].target = strdup(target);
        alerts[alerts_size].extra = strdup(extra);
        alerts[alerts_size].body_tmpl = strdup(body_tmpl);

        printf("\t%s: %s\n",
            alerts[alerts_size].target,
            type_str[alerts[alerts_size].type]
        );

        alerts_size++;
    }

    return alerts_size;
}

void
alert_trigger(const target_t *target)
{
    static const int (*send_funcs[])(const target_t *, const char *,
        const char *, const char *) = 
    {
        send_api,
        send_email
    };

    static char timestr[256];

    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    strftime(timestr, 256, "%F %T", tm_now);

    for (int i = 0; i < alerts_size; i++) {
        printf("[%s] [monitor] alerted %s about %s\n",
            timestr, alerts[i].target, target->name);
        send_funcs[target->type](target, alerts[i].target, alerts[i].extra,
            alerts[i].body_tmpl);
    }
}

