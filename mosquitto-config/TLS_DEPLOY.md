# TLS de extremo a extremo: nodo → broker → backend

El anteproyecto compromete "transmisión segura de la información". Este
documento describe cómo queda implementada y cómo desplegarla.

## Por qué así

- El proxy TCP de Railway (`shinkansen.proxy.rlwy.net:58954`) es **passthrough
  crudo**: no termina TLS. El cifrado debe servirlo el propio Mosquitto.
- El dominio del proxy no es nuestro, así que ninguna CA pública (Let's
  Encrypt) emite certificados para él. Se usa una **CA propia del proyecto**:
  el backend y el firmware la anclan (pinning), lo que además de cifrar
  autentica que se habla con *nuestro* broker (protección MitM real).
- Los clientes siguen autenticándose con usuario/contraseña + ACL; el
  certificado solo autentica al servidor (`require_certificate false`).

## Componentes ya implementados

| Pieza | Dónde | Estado |
|---|---|---|
| Listener TLS 8883 | `mosquitto.conf` | Configurado (requiere certs en el volumen) |
| Generador de certificados | `certs/generar_certificados.sh` | CA + cert del broker con SAN del proxy |
| Backend con CA anclada | `app/config.py` → `MQTT_TLS_CA_FILE` | Listo, por defecto usa CAs del sistema |
| Firmware SIM7080G | `METER_MQTT_USE_TLS` en `meter_config.h` | Implementado, **deshabilitado por defecto** |
| CA embebida en firmware | `config/mqtt_ca_cert.h` | Placeholder — pegar `ca.crt` generado |
| Métrica de overhead | log `MQTT connected (X ms, TLS=Y)` | Activa siempre |

## Despliegue (en orden — el sistema sigue operando en claro hasta el paso 5)

1. **Generar certificados** (una vez, en la máquina del tesista):
   ```bash
   cd mosquitto-config/certs
   ./generar_certificados.sh shinkansen.proxy.rlwy.net
   ```
   `ca.key` no sale de esa máquina; nada de `certs/` entra a git (.gitignore).

2. **Subir al volumen del broker en Railway**: copiar `ca.crt`, `server.crt`
   y `server.key` a `/mosquitto/config/certs/` (mismo mecanismo que el
   `passwd`, ver DEPLOY.md) y redeploy. En logs debe aparecer
   `Opening ipv4 listen socket on port 8883`.

3. **Apuntar un proxy TCP de Railway al puerto 8883** (Settings → Networking
   del servicio Mosquitto). Anotar el `host:puerto` externo asignado.

4. **Backend**: en Railway, variables del servicio backend:
   - `MQTT_PORT` = puerto externo nuevo (el que mapea a 8883)
   - `MQTT_TLS_CA_FILE` = ruta al `ca.crt` (subirlo junto al código o como
     archivo en el volumen; con repo: `mosquitto-config/certs/ca.crt` **no**
     está en git — copiarlo a un secreto/volumen del servicio)

5. **Firmware**: en `config/mqtt_ca_cert.h` pegar el PEM de `ca.crt`; en
   `config/meter_config.h` poner `METER_MQTT_USE_TLS 1` y `METER_MQTT_PORT`
   al puerto del paso 3. Compilar (`pio run`) y flashear.

6. **Retirar el listener 1883** de `mosquitto.conf` cuando todos los nodos
   estén migrados.

### Rollback

Si los nodos en campo no logran el handshake: volver `METER_MQTT_USE_TLS 0`
y `METER_MQTT_PORT` al puerto plano — el listener 1883 sigue activo durante
la transición precisamente para esto.

## Protocolo de medición del overhead TLS (capítulo de ciberseguridad)

El firmware ya registra la duración de cada establecimiento de sesión:

```
mqtt_client: MQTT connected (3420 ms, TLS=0)
```

**Procedimiento** (mismo nodo, misma celda, misma SIM):

1. Flashear con `METER_MQTT_USE_TLS 0`. Provocar 10 reconexiones (comando
   remoto `reiniciar` o ciclo de alimentación) y anotar los `X ms` del log.
2. Flashear con `METER_MQTT_USE_TLS 1` y repetir las 10 reconexiones.
3. Reportar mediana y rango de ambos grupos. La diferencia de medianas es
   el costo del handshake TLS 1.2 sobre LTE-M (esperable: +2 a +6 s por el
   RTT del intercambio de certificados a 4 KB y la verificación RSA en el
   módem).
4. **Consumo**: si hay analizador de corriente (perfil del nodo en
   reconexión), integrar la corriente durante la ventana de conexión en
   ambos modos; el delta de carga (mAh) por reconexión × reconexiones/día
   estima el costo energético diario del TLS. Sin analizador, aproximar
   con corriente típica de TX del SIM7080G (~susceptible de citarse del
   datasheet) × el delta de tiempo medido.
5. **Tamaño de tramas**: opcionalmente capturar en el broker
   (`log_type debug`) los bytes de handshake para reportar el overhead de
   datos (~5-7 KB por sesión TLS vs ~100 B del CONNECT plano), relevante
   en planes NB-IoT/LTE-M tarificados por datos.

Registrar los resultados en `Documentos/` para el informe final.
