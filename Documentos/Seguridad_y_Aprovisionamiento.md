# Seguridad y aprovisionamiento de medidores

**Fecha:** 2026-07-02 · **Alcance:** backend, frontend, broker MQTT, firmware, repositorio.

Este documento consolida (1) el procedimiento de rotación de la credencial
de PostgreSQL expuesta, (2) el inventario de brechas encontradas y su
estado, y (3) el diseño para conectar muchos medidores en modo
"plug and play".

---

## 1. Rotación de la contraseña de PostgreSQL (URGENTE)

Los scripts `migrate_nodo_salud.py` y `migrate_imei_reconciliacion.py`
llevaban la URL completa de la base de producción (usuario + contraseña)
commiteada en un repositorio público de GitHub. Los archivos ya se
eliminaron del repo, **pero la contraseña sigue en el historial de git**:
debe considerarse comprometida y rotarse. Pasos en Railway:

1. Dashboard Railway → proyecto `energetic-curiosity` → servicio **Postgres**.
2. Pestaña **Data** (o **Connect** → `railway connect`) para abrir una consola SQL, y ejecutar:
   ```sql
   ALTER USER postgres WITH PASSWORD 'NUEVA_CLAVE_LARGA_Y_ALEATORIA';
   ```
   (generarla con `openssl rand -base64 24` o un gestor de contraseñas; no reutilizar `Colombia2026$`).
3. En el servicio Postgres → **Variables**, actualizar `PGPASSWORD` y `DATABASE_URL` con la clave nueva (si el backend referencia `${{Postgres.DATABASE_URL}}`, se propaga solo).
4. En el servicio **BACKEND** → Deployments → **Redeploy** para que tome la variable nueva.
5. Verificar: `https://tesisproject-production.up.railway.app/health` y que lleguen datos al dashboard.
6. Opcional pero recomendado: reescribir el historial con `git filter-repo --invert-paths --path energia-iot-backend/migrate_nodo_salud.py --path energia-iot-backend/migrate_imei_reconciliacion.py` y force-push. Si no se hace, la clave vieja queda visible en el historial — por eso la rotación es obligatoria y suficiente.

## 2. Inventario de brechas

| # | Brecha | Riesgo | Estado |
|---|---|---|---|
| 1 | Contraseña de PostgreSQL en scripts commiteados | Acceso total a la BD de producción desde internet | Scripts eliminados; **rotación pendiente (manual, pasos arriba)** |
| 2 | Endpoints `/admin/*` (seed, limpiar, diagnóstico) sin autenticación | Cualquiera podía sembrar datos falsos, borrar dispositivos o leer el estado de la BD | **Corregido**: candado `require_super_admin` a nivel de router |
| 3 | `POST /mediciones` público | Inyección de mediciones falsas a nombre de un medidor real | **Corregido**: requiere operador/admin (la ingesta real va por MQTT) |
| 4 | `SECRET_KEY` con valor por defecto si no se define la variable | Forja de tokens JWT de admin | **Mitigado**: log crítico al arrancar en producción; **definir `SECRET_KEY` en Railway** (`openssl rand -hex 32`) |
| 5 | Credencial MQTT compartida (`medidor_iot`/`Colombia2026$`) en `meter_config.h` y `DEPLOY.md`, commiteada | Un nodo comprometido (o cualquiera que lea el repo) puede publicar/suplantar a toda la flota | Pendiente: rotar y migrar al esquema por-IMEI (sección 3); requiere reflashear |
| 6 | Enlace MQTT nodo→broker sin TLS | Sniffing de credenciales y datos en la red celular | Implementado y **apagado por defecto** (`METER_MQTT_USE_TLS`); activar con `mosquitto-config/TLS_DEPLOY.md` |
| 7 | Endpoints GET (mediciones, eventos, dashboard) sin token | Lectura anónima de telemetría | Aceptado por ahora (facilita la demo); cerrar antes de un despliegue comercial exigiendo JWT también en lectura |
| 8 | Sin límite de intentos en `/auth/login` | Fuerza bruta de contraseñas | Pendiente (baja prioridad en prototipo; anotar en trabajo futuro) |
| 9 | Contraseña del admin por defecto (`admin123`) | Acceso admin trivial si no se cambia | **Cambiarla desde Perfil** en producción |

## 3. Plug and play: conectar N medidores sin tocar la plataforma

Lo que ya funciona hoy:

- **Auto-registro**: el primer mensaje MQTT de un `device_id` desconocido crea el dispositivo en la BD (y el retiro lógico se revierte solo si el equipo vuelve).
- **Identidad desde el equipo**: el nodo reporta IMEI y versión de firmware por `/estado`; la plataforma los muestra sin configuración manual.
- **Multi-nodo en datos**: mediciones, salud y eventos están keyed por `device_id`; el dashboard agrega la flota completa.

Lo que falta para que conectar el medidor N+1 sea solo "flashear y encender":

1. **Topics derivados del IMEI en el firmware** (cambio principal). Hoy
   `METER_MQTT_TOPIC_*` y `METER_MQTT_CLIENT_ID` son constantes de
   compilación (`ESP32-001`, `medidor_cristian_001`): dos nodos con el mismo
   binario colisionarían. Propuesta: tras leer el IMEI en el arranque de
   task_communication, construir en runtime
   `medidor/<IMEI>/{datos,estado,alerta,conexion,cmd}` y usar el IMEI como
   CLIENTID. Un único binario sirve para toda la flota.
2. **Credencial MQTT por dispositivo**: username = IMEI, clave única
   grabada en NVS durante el aprovisionamiento (no en el código). El ACL ya
   quedó preparado con la regla `pattern readwrite medidor/%u/#`
   (comentada en `mosquitto-config/acl`): cada nodo queda confinado a su
   rama y no puede suplantar a otro.
3. **Alta en el broker**: un comando por medidor
   (`mosquitto_passwd -b passwd <IMEI> <clave>`). Automatizable después con
   un endpoint de aprovisionamiento en el backend que genere la clave,
   la registre en el broker y devuelva un QR para el instalador.
4. **Umbrales por dispositivo**: el nominal CREG es global
   (`VOLTAJE_NOMINAL`); con flota mixta 110/120 V conviene mover el umbral
   a columna de `dispositivos` (la columna `factor_voltaje` ya existe como
   precedente).
5. **Retención de datos**: a 1 medición/min por nodo son ~525k filas/año
   por medidor. Antes de pasar de ~10 nodos, agregar tabla de promedios
   horarios y purga de crudos > 90 días (la consulta histórica ya está
   preparada: `resolucion` hora/día).

Orden sugerido: (1)+(2) juntos en el firmware —es una sola pasada sobre
mqtt_client.c/task_communication.c—, luego (3) manual para los primeros
nodos, y (4)(5) cuando haya más de un medidor físico en campo.
