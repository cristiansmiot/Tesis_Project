#ifndef DATA_SERIALIZER_H
#define DATA_SERIALIZER_H

/**
 * @file data_serializer.h
 * @brief Data Serializer - SenML RFC 8428
 *
 * Topics:
 *   /datos   -> electrical measurements
 *   /estado  -> node health and connectivity state
 *   /alerta  -> power quality events
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "meter_data.h"
#include "system_contracts.h"

esp_err_t data_serializer_init(void);

/* Side-effect free: caller owns the immutable identity shared by SD/MQTT. */
esp_err_t data_serializer_build_senml_datos_record(const MeterData_t *snap,
                                                   const system_record_base_t *base,
                                                   char *out, size_t out_len);

esp_err_t data_serializer_build_senml_datos(const MeterData_t *snap,
                                             char *out,
                                             size_t out_len);

/**
 * Legacy helper creating a NEW identity with an explicit UTC base time.
 * Retries must reuse a serialized packet or the _record() variant above.
 */
esp_err_t data_serializer_build_senml_datos_at(const MeterData_t *snap,
                                               double bt_epoch,
                                               char *out,
                                               size_t out_len);

esp_err_t data_serializer_build_senml_estado(const MeterData_t *snap,
                                              bool cellular_ok,
                                              bool mqtt_ok,
                                              bool cal_ok,
                                              char *out,
                                              size_t out_len);

esp_err_t data_serializer_build_senml_alerta(const MeterData_t *snap,
                                              uint32_t pq_flags,
                                              char *out,
                                              size_t out_len);

/** Serializa un evento comun de salud para el topic /alerta. */
esp_err_t data_serializer_build_senml_system_event(const system_event_t *event,
                                                   char *out,
                                                   size_t out_len);

esp_err_t data_serializer_build_snapshot_json(const MeterData_t *snap,
                                              char *out_json,
                                              size_t out_len);

esp_err_t data_serializer_build_current_json(char *out_json, size_t out_len);

#endif // DATA_SERIALIZER_H
