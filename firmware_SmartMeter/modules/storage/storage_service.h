#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define STORAGE_SERVICE_PAYLOAD_MAX 768U
#define STORAGE_SERVICE_LINE_MAX    1024U

typedef enum {
    STORAGE_RECORD_HISTORY = 0,
    STORAGE_RECORD_TELEMETRY,
    STORAGE_RECORD_ALERT,
    STORAGE_RECORD_EVENT,
} storage_record_kind_t;

typedef struct {
    bool mounted;
    uint32_t total_mb;
    uint32_t free_mb;
    uint8_t used_percent;
    uint32_t writes_ok;
    uint32_t writes_failed;
    uint32_t requests_pending;
    uint32_t requests_dropped;
    uint32_t telemetry_pending;
    uint32_t alerts_pending;
    uint32_t data_loss_records;
    uint64_t data_loss_bytes;
} storage_service_snapshot_t;

esp_err_t storage_service_start(void);
esp_err_t storage_service_submit_telemetry(const char *json_payload);
esp_err_t storage_service_submit_alert(const char *json_payload);
esp_err_t storage_service_submit_event(const char *json_line);
esp_err_t storage_service_peek_pending(storage_record_kind_t kind,
                                       char *out,
                                       size_t out_len);
esp_err_t storage_service_ack_pending(storage_record_kind_t kind);
esp_err_t storage_service_get_snapshot(storage_service_snapshot_t *out);

#if defined(STORAGE_SERVICE_TEST_HOOKS) && (STORAGE_SERVICE_TEST_HOOKS != 0)
void storage_service_test_inject_usage(uint32_t total_mb, uint32_t free_mb);
void storage_service_test_clear_usage(void);
#endif

#endif /* STORAGE_SERVICE_H */
