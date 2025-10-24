#ifndef _MONITOR_H
#define _MONITOR_H

int monitor_init(const char *cfg_file, const char *log_file);
const char *monitor_generate_status_html();
const char *monitor_generate_incidents_html();
void monitor_check();

#endif /* _MONITOR_H */
