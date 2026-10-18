#ifndef SYSTEM_HEALTH_H
#define SYSTEM_HEALTH_H

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "system_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t system_health_init(void);
bool system_health_is_initialized(void);

esp_err_t system_health_set_disabled(system_module_t module, const char *detail);
esp_err_t system_health_mark_recovering(system_module_t module, const char *detail);
esp_err_t system_health_report_failure(system_module_t module,
                                       system_event_code_t code,
                                       system_severity_t severity,
                                       esp_err_t error,
                                       const char *detail);
esp_err_t system_health_report_ok(system_module_t module, const char *detail);
esp_err_t system_health_report_occurrence(system_module_t module,
                                          system_event_code_t code,
                                          system_severity_t severity,
                                          esp_err_t error,
                                          const char *detail);

esp_err_t system_health_get_snapshot(system_module_t module,
                                     system_health_snapshot_t *out);
esp_err_t system_health_get_all_snapshots(system_health_snapshot_t *out,
                                          size_t count);
bool system_health_has_active_critical(void);

esp_err_t system_event_receive(system_event_t *out, TickType_t wait_ticks);
uint32_t system_event_get_dropped_count(void);
uint32_t system_event_get_pending_count(void);

bool system_time_epoch_is_valid(int64_t epoch_utc_s);
esp_err_t system_time_mark_valid(system_time_source_t source,
                                 int64_t epoch_utc_s);
void system_time_mark_invalid(const char *detail);
bool system_time_is_valid(void);
esp_err_t system_time_get_status(system_time_status_t *out);

/** Crea la cabecera comun e incrementa una secuencia unica por arranque. */
esp_err_t system_record_base_create(system_record_type_t type,
                                    system_record_base_t *out);

const char *system_module_name(system_module_t module);
const char *system_state_name(system_state_t state);
const char *system_severity_name(system_severity_t severity);
const char *system_event_transition_name(system_event_transition_t transition);
const char *system_event_code_name(system_event_code_t code);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_HEALTH_H */
