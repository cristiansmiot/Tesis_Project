#include "mqtt_topics.h"

#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "logger.h"
#include "meter_config.h"

static const char *TAG = "mqtt_topics";

// "SM-" + 12 hex de la MAC + NUL, con margen para el fallback configurado.
// Los buffers de topic cubren el peor caso: "medidor/" + id + "/conexion".
static char s_device_id[32];
static char s_topic_datos[64];
static char s_topic_estado[64];
static char s_topic_alerta[64];
static char s_topic_conexion[64];
static char s_topic_cmd[64];
static bool s_inicializado;

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
    if (s_inicializado) {
        return ESP_OK;
    }

    uint8_t mac[6] = {0};
    const esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err == ESP_OK) {
        char id[24];
        (void)snprintf(id, sizeof(id), "%s-%02X%02X%02X%02X%02X%02X",
                       METER_MQTT_ID_PREFIX,
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        mqtt_topics_build(id);
        LOG_INFO(TAG, "identidad del nodo: %s (MAC eFuse)", s_device_id);
    } else {
        // Practicamente imposible (la MAC eFuse viene de fabrica), pero un
        // nodo sin identidad valida es peor que uno con id generico.
        mqtt_topics_build(METER_MQTT_DEVICE_ID_FALLBACK);
        LOG_ERROR(TAG, "MAC eFuse ilegible (%s) - id de respaldo: %s",
                  esp_err_to_name(err), s_device_id);
    }

    s_inicializado = true;
    return ESP_OK;
}

// Los getters se auto-inicializan para que ningun consumidor temprano
// (UI pintando el id en el splash, LWT del perfil MQTT) reciba una
// cadena vacia por orden de arranque.
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
