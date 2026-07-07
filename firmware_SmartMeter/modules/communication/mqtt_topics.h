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
 * El device_id se deriva de la MAC de fábrica del ESP32-S3 (eFuse BLK1,
 * grabada por Espressif e inmutable), con el formato SM-XXXXXXXXXXXX.
 * Se descartó usar el IMEI como identidad primaria: el IMEI pertenece al
 * módem SIM7080G, así que sobrevive un cambio de SIM pero no una
 * reparación que reemplace el módulo celular; la MAC del MCU acompaña al
 * medidor durante toda su vida útil y además está disponible desde el
 * primer instante del boot, sin esperar a que el módem responda. El IMEI
 * sigue viajando como metadato en /estado para trazabilidad de la parte
 * celular.
 *
 * Con la identidad en runtime un mismo binario sirve para toda la flota:
 * los topics medidor/<id>/{datos,estado,alerta,conexion,cmd} no chocan
 * entre nodos y el backend auto-registra cada id nuevo.
 */

/**
 * Deriva el device_id de la MAC eFuse y construye los topics. Idempotente
 * y sin dependencias de hardware externo; puede llamarse desde app_main.
 */
esp_err_t mqtt_topics_init(void);

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
