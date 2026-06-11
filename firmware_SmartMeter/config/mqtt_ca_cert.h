#ifndef MQTT_CA_CERT_H
#define MQTT_CA_CERT_H

/**
 * Certificado de la CA propia del proyecto en formato PEM.
 *
 * Se genera con mosquitto-config/certs/generar_certificados.sh y se pega
 * aqui el contenido de ca.crt. El firmware lo sube al filesystem del
 * SIM7080G (AT+CFSWFILE) y lo registra para la sesion MQTT (AT+SMSSL),
 * de modo que el modem valide que habla con NUESTRO broker y no con un
 * impostor (el cifrado sin validacion de CA no protege contra MitM).
 *
 * Publicar la CA en el repositorio no es un riesgo: es la parte publica;
 * la clave privada (ca.key) nunca sale de la maquina del tesista.
 *
 * Mientras conserve el texto "PEGAR-CA-AQUI", mqtt_client.c rechaza el
 * arranque TLS con un error claro en el log en vez de fallar a mitad del
 * handshake con un error criptico del modem.
 */
static const char MQTT_CA_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "PEGAR-CA-AQUI\n"
    "-----END CERTIFICATE-----\n";

#endif // MQTT_CA_CERT_H
