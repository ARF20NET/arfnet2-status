#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <microhttpd.h>

#include "monitor.h"
#include "config.h"
#include "check.h"
#include "alert.h"


static char *index_format_template = NULL;


enum MHD_Result answer_to_connection(
    void *cls, struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **ptr
) {
    char buff[BUFF_SIZE];

    const struct sockaddr_in **coninfo =
        (const struct sockaddr_in**)MHD_get_connection_info(
            connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    static char timestr[256];
    strftime(timestr, 256, "%Y-%m-%d %H:%M:%S", tm_now);

    printf("[%s] [webserver] %s %s %s: ",
        timestr, inet_ntoa((*coninfo)->sin_addr), method, url);

    struct MHD_Response *response;
    int ret;

    if (strcmp(method, "GET") == 0 && strcmp(url, "/") == 0) {
        snprintf(buff, BUFF_SIZE,
            index_format_template,
            timestr,
            monitor_generate_status_html(),
            monitor_generate_incidents_html());

        response = MHD_create_response_from_buffer(strlen(buff), (void*)buff,
            MHD_RESPMEM_PERSISTENT);

        printf("%d\n", 200);
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    }
    else {
        response = MHD_create_response_from_buffer(0, (void*)NULL, 0);
        printf("%d\n", 418);
        ret = MHD_queue_response(connection, 418, response);
        MHD_destroy_response(response);
    }
    return ret;
}

int main() {
    printf("ARFNET Status Monitor (C) 2025 under GPLv3\n");

    if (config_load(CONFIG_PATH) < 0)
        return 1;

    /* read index template file */
    FILE *tf = fopen(tmpl_path, "r");
    if (!tf) {
        fprintf(stderr, "error opening index template file: %s\n",
            strerror(errno));
        return 1;
    }
    fseek(tf, 0, SEEK_END);
    size_t tfs = ftell(tf);
    rewind(tf);
    index_format_template = malloc(tfs);
    fread(index_format_template, 1, tfs, tf);
    fclose(tf);

    if (check_init() < 0)
        return 1;

    if (monitor_init() < 0)
        return 1;
    
    if (alert_init() < 0)
        return 1;

    /* start server */
    struct MHD_Daemon *daemon;

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_EPOLL,
        port, NULL, NULL,
        &answer_to_connection, NULL, MHD_OPTION_END);

    if (!daemon) {
        fprintf(stderr, "error starting libmicrohttpd daemon: \n");
        return 1;
    }

    while (1) {
        check_perform(targets, targets_size);
        monitor_update_events(log_path);
        sleep(monitor_config.interval);
    }
}

