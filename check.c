#include "check.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <curl/curl.h>

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
check_perform(target_t *targets, size_t targets_n)
{
    static size_t check_num = 0;
    static char timestr[256];

    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    strftime(timestr, 256, "%F %T", tm_now);

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

int
check_init()
{
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res) {
        fprintf(stderr, "Error initializing cURL: %s\n",
            curl_easy_strerror(res));
        return -1;
    }

    return 0;
}


