#ifndef _ALERT_H
#define _ALERT_H

#include "monitor.h"

int alert_init();
void alert_trigger(const target_t *target);

#endif /* _ALERT_H */

