#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "system_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool rtc_ready;
    bool rtc_time_valid;
    bool rtc_oscillator_stopped;
    bool rtc_eosc_set;
    time_t rtc_epoch_utc;
    float rtc_temperature_c;
    esp_err_t rtc_last_error;
    uint32_t rtc_consecutive_failures;
} time_service_snapshot_t;

esp_err_t time_service_init(void);

/** Encola una fuente autoritativa; la tarea de tiempo aplica el cambio. */
esp_err_t time_service_submit_sync(system_time_source_t source,
                                   int64_t epoch_utc_s);

/** Uso exclusivo de task_time durante arranque y recuperacion. */
esp_err_t time_service_apply_rtc_bootstrap(void);
esp_err_t time_service_poll_rtc(void);
esp_err_t time_service_process_pending_sync(void);

esp_err_t time_service_get_snapshot(time_service_snapshot_t *out);
esp_err_t time_service_format_bogota(char *out, size_t out_len,
                                     bool include_seconds);
const char *time_service_source_name(system_time_source_t source);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SERVICE_H */
