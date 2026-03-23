# Despliegue de Configuración Mosquitto en Railway

## Archivos de este directorio

| Archivo | Descripción |
|---------|-------------|
| `mosquitto.conf` | Configuración principal del broker |
| `acl` | Control de acceso por topic |
| `DEPLOY.md` | Este documento |

## Pasos para aplicar en Railway

### 1. Acceder al servicio Mosquitto en Railway

1. Ir al dashboard del proyecto `energetic-curiosity` en Railway
2. Clic en el servicio **Mosquitto Broker**
3. Ir a la pestaña **Settings**

### 2. Verificar el comando de inicio

El comando de inicio debe incluir la ruta al archivo de configuración:

```
mosquitto -c /mosquitto/config/mosquitto.conf
```

Si no es así, actualizar en Settings → Start Command.

### 3. Copiar archivos al volumen montado

Railway monta el volumen en el path configurado del servicio.
Para subir los archivos de configuración:

**Opción A — Railway CLI (recomendada):**
```bash
# Instalar Railway CLI si no está instalado
npm install -g @railway/cli

# Login
railway login

# Conectar al proyecto
railway link

# Copiar archivos al volumen del servicio Mosquitto
railway run --service "Mosquitto Broker" cp mosquitto.conf /mosquitto/config/mosquitto.conf
railway run --service "Mosquitto Broker" cp acl /mosquitto/config/acl
```

**Opción B — Montar como volumen desde repositorio:**
En la configuración del servicio Mosquitto en Railway, configurar el
Build Command para copiar los archivos desde el repo:
```
cp /app/mosquitto-config/mosquitto.conf /mosquitto/config/mosquitto.conf && \
cp /app/mosquitto-config/acl /mosquitto/config/acl
```

### 4. Generar el archivo de contraseñas

El archivo `passwd` NO se versiona en git (contiene credenciales).
Generarlo directamente en el contenedor del broker:

```bash
# Dentro del contenedor Railway del broker:
mosquitto_passwd -c /mosquitto/config/passwd medidor_iot
# Ingresar la contraseña cuando se solicite: Colombia2026$
```

O en forma no-interactiva:
```bash
mosquitto_passwd -b /mosquitto/config/passwd medidor_iot Colombia2026$
```

### 5. Reiniciar el servicio

Desde el dashboard de Railway:
1. Clic en **Deployments**
2. Clic en **Redeploy** (o reiniciar el contenedor)

### 6. Verificar logs

En Railway → Mosquitto Broker → **Logs**, verificar que aparezcan:
```
2026-03-21T10:00:00 mosquitto version X.X.X starting
2026-03-21T10:00:00 Config loaded from /mosquitto/config/mosquitto.conf
2026-03-21T10:00:00 Opening ipv4 listen socket on port 1883.
```

## Topics y su comportamiento con retain=1

Después de aplicar la configuración, estos topics tienen comportamiento especial:

| Topic | retain | Efecto |
|-------|--------|--------|
| `medidor/ESP32-001/conexion` | 1 | El último estado (online/offline) se entrega inmediatamente a nuevos suscriptores |
| `medidor/ESP32-001/estado` | 1 | El backend recibe el último estado del nodo al conectar, sin esperar 5 min |
| `medidor/ESP32-001/datos` | 0 | Solo tiempo real, no se retiene |
| `medidor/ESP32-001/alerta` | 0 | Evento puntual, no se retiene |

## Verificar LWT con MQTT Explorer

Para comprobar que el LWT funciona:

1. Conectarse al broker con [MQTT Explorer](https://mqtt-explorer.com/)
   - Host: `shinkansen.proxy.rlwy.net`
   - Port: `58954`
   - Usuario: `medidor_iot`
2. Suscribirse a `medidor/#`
3. Apagar el ESP32 sin gracias (quitar alimentación)
4. En ~90s el broker publica automáticamente `offline` en `medidor/ESP32-001/conexion`
