#include "mqtt_topics.h"

#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "meter_config.h"
#include "sim7080g_init.h"

static const char *TAG = "mqtt_topics";

// 15 dígitos de IMEI + margen para el fallback configurado. Los buffers
// de topic cubren el peor caso: "medidor/" + id + "/conexion" + NUL.
static char s_device_id[32];
static char s_topic_datos[64];
static char s_topic_estado[64];
static char s_topic_alerta[64];
static char s_topic_conexion[64];
static char s_topic_cmd[64];
static bool s_inicializado;
static bool s_id_es_imei;

static void mqtt_topics_build(const char *device_id)
{
    (void)snprintf(s_device_id, sizeof(s_device_id), "%s", device_id);
    (void)snprintf(s_topic_datos, sizeof(s_topic_datos), "medidor/%s/datos", s_device_id);
    (void)snprintf(s_topic_estado, sizeof(s_topic_estado), "medidor/%s/estado", s_device_id);
    (void)snprintf(s_topic_alerta, sizeof(s_topic_alerta), "medidor/%s/alerta", s_device_id);
    (void)snprintf(s_topic_conexion, sizeof(s_topic_conexion), "medidor/%s/conexion", s_device_id);
    (void)snprintf(s_topic_cmd, sizeof(s_topic_cmd), "medidor/%s/cmd", s_device_id);
}

esp_err_t mqtt_topics_init(void)
{
    // Con la identidad ya fijada desde el IMEI no se recalcula: el broker
    // retiene /estado y /conexion por topic, y cambiar de id a mitad de
    // sesion dejaria mensajes huerfanos bajo el id anterior.
    if (s_inicializado && s_id_es_imei) {
        return ESP_OK;
    }

#if (METER_MQTT_ID_FROM_IMEI != 0)
    const char *imei = sim7080g_get_imei();
    if ((imei != NULL) && (imei[0] != '\0')) {
        mqtt_topics_build(imei);
        s_id_es_imei = true;
        s_inicializado = true;
        LOG_INFO(TAG, "identidad MQTT desde IMEI: %s", s_device_id);
        return ESP_OK;
    }
#endif

    if (!s_inicializado) {
        mqtt_topics_build(METER_MQTT_DEVICE_ID_FALLBACK);
        s_inicializado = true;
        LOG_WARN(TAG, "IMEI no disponible - usando id de respaldo: %s", s_device_id);
    }
    return ESP_OK;
}

bool mqtt_topics_id_es_imei(void)
{
    return s_id_es_imei;
}

// Los getters se auto-inicializan con el fallback para que un uso temprano
// (p.ej. UI pintando el id antes de que el modem registre) nunca devuelva
// una cadena vacia.
#define MQTT_TOPICS_GETTER(nombre, buffer)      \
    const char *nombre(void)                    \
    {                                           \
        if (!s_inicializado) {                  \
            (void)mqtt_topics_init();           \
        }                                       \
        return (buffer);                        \
    }

MQTT_TOPICS_GETTER(mqtt_topics_device_id, s_device_id)
MQTT_TOPICS_GETTER(mqtt_topics_datos, s_topic_datos)
MQTT_TOPICS_GETTER(mqtt_topics_estado, s_topic_estado)
MQTT_TOPICS_GETTER(mqtt_topics_alerta, s_topic_alerta)
MQTT_TOPICS_GETTER(mqtt_topics_conexion, s_topic_conexion)
MQTT_TOPICS_GETTER(mqtt_topics_cmd, s_topic_cmd)
