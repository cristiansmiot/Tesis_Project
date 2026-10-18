#ifndef SYSTEM_EVENT_DISPATCHER_H
#define SYSTEM_EVENT_DISPATCHER_H

#include "system_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t system_event_dispatcher_start(void);
esp_err_t system_alert_peek(system_event_t *out);
esp_err_t system_alert_discard(void);
uint32_t system_alert_pending_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_EVENT_DISPATCHER_H */
