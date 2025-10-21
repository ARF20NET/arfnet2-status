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

#define PORT        8888
#define RES_BUFF    65535

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
    char buff[RES_BUFF];

    const struct sockaddr_in **coninfo =
        (const struct sockaddr_in**)MHD_get_connection_info(
            connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    time_t time_now = time(NULL);
    struct tm *tm_now = gmtime(&time_now);
    static char timestr[256];
    strftime(timestr, 256, "%Y-%m-%d %H-%M-%S", tm_now);

    printf("[%s] [webserver] %s %s %s: ",
        timestr, inet_ntoa((*coninfo)->sin_addr), method, url);

    struct MHD_Response *response;
    int ret;

    if (strcmp(method, "GET") == 0 && strcmp(url, "/") == 0) {
        snprintf(buff, RES_BUFF,
            index_format_template,
            monitor_generate_status_html(), "(incidents)");

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
    /* read index template file */
    FILE *tf = fopen("index.htm.tmpl", "r");
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

    /* start server */
    struct MHD_Daemon *daemon;

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_EPOLL,
        PORT, NULL, NULL,
        &answer_to_connection, NULL, MHD_OPTION_END);

    if (!daemon) {
        fprintf(stderr, "error starting libmicrohttpd daemon: \n");
        return 1;
    }

    monitor_init("monitor.cfg", "events.log");

    while (1) {
        monitor_check();
        sleep(5);
    }
}

