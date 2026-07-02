#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Identidad MQTT del nodo resuelta en tiempo de ejecución.
 *
 * Con topics fijos en meter_config.h dos nodos con el mismo binario
 * colisionaban (ambos publicaban como ESP32-001). Aquí el device_id se
 * toma del IMEI del SIM7080G — único por módem, ya disponible tras
 * sim7080g_init() — y los topics medidor/<id>/{datos,estado,alerta,
 * conexion,cmd} se construyen una sola vez. El mismo binario sirve para
 * toda la flota y el backend auto-registra cada IMEI nuevo.
 *
 * Si el IMEI no está disponible (modo focus sin red, módem dañado) se usa
 * METER_MQTT_DEVICE_ID_FALLBACK para no dejar el nodo mudo.
 */

/**
 * Resuelve el device_id y construye los topics. Idempotente: la primera
 * llamada con IMEI disponible fija la identidad; llamadas posteriores no
 * la cambian (los topics no deben mutar con la sesión MQTT ya abierta).
 */
esp_err_t mqtt_topics_init(void);

/** @return true si la identidad ya quedó fijada desde el IMEI real. */
bool mqtt_topics_id_es_imei(void);

const char *mqtt_topics_device_id(void);
const char *mqtt_topics_datos(void);
const char *mqtt_topics_estado(void);
const char *mqtt_topics_alerta(void);
const char *mqtt_topics_conexion(void);
const char *mqtt_topics_cmd(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_TOPICS_H
