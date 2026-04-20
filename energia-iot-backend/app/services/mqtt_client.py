"""
==============================================================================
SERVICIO: Cliente MQTT para Backend FastAPI
==============================================================================

Este módulo conecta el backend FastAPI al broker Mosquitto en Railway.
Se suscribe a los topics del medidor de energía y guarda las mediciones
automáticamente en PostgreSQL.

Arquitectura:
    ESP32/SIM7080G → MQTT (Mosquitto Railway) → Este módulo → PostgreSQL

Topics MQTT:
    medidor/+/datos      → Mediciones de energía (JSON)
    medidor/+/estado     → Estado del dispositivo (JSON)
    medidor/+/alerta     → Alertas del dispositivo (JSON)

Autor: Cristian - Tesis Maestría IoT, Universidad Javeriana
Fecha: Febrero 2026
"""

import json
import logging
import os
import ssl
import threading
import time
import uuid
from datetime import datetime, timezone
from typing import Optional

import paho.mqtt.client as mqtt

# Configurar logging
logger = logging.getLogger("mqtt_client")
logger.setLevel(logging.INFO)


class MQTTClient:
    """
    Cliente MQTT que se suscribe al broker Mosquitto en Railway
    y procesa los mensajes del medidor de energía.
    """

    def __init__(
        self,
        broker_host: str,
        broker_port: int,
        username: str,
        password: str,
        topic_base: str = "medidor/#",
        client_id: str = "",
        use_tls: bool = False,
    ):
        """
        Inicializa el cliente MQTT.

        Args:
            broker_host: Host del broker (ej: shinkansen.proxy.rlwy.net)
            broker_port: Puerto TCP del broker (ej: 58954)
            username: Usuario MQTT (ej: medidor_iot)
            password: Contraseña MQTT
            topic_base: Topic base para suscripción
            client_id: ID único del cliente
            use_tls: Habilitar TLS para conexión segura (Railway lo requiere)
        """
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.username = username
        self.password = password
        self.topic_base = topic_base
        self.client_id = client_id or self._generate_client_id()
        self.use_tls = use_tls

        # Cliente MQTT (paho-mqtt v2)
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=self.client_id,
            protocol=mqtt.MQTTv311,
        )
        self.client.username_pw_set(self.username, self.password)

        # Habilitar TLS si es necesario (Railway proxy lo requiere)
        if self.use_tls:
            self.client.tls_set(
                cert_reqs=ssl.CERT_REQUIRED,
                tls_version=ssl.PROTOCOL_TLS_CLIENT,
            )
            logger.info("🔒 TLS habilitado para conexión MQTT")

        self.client.enable_logger(logger)
        self.client.reconnect_delay_set(min_delay=2, max_delay=30)

        # Callbacks
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message

        # Estado
        self.is_connected = False
        self._db_session_factory = None
        self._thread: Optional[threading.Thread] = None
        self._should_stop = False

        # Estadísticas
        self.messages_received = 0
        self.messages_saved = 0
        self.last_message_at: Optional[datetime] = None

    @staticmethod
    def _generate_client_id() -> str:
        """Genera client_id único por proceso para evitar colisiones entre instancias."""
        return f"backend_{os.getpid()}_{uuid.uuid4().hex[:6]}"

    def set_db_session_factory(self, session_factory):
        """
        Configura la fábrica de sesiones de base de datos.
        Se llama desde main.py al iniciar la aplicación.
        """
        self._db_session_factory = session_factory
        logger.info("✅ Fábrica de sesiones de BD configurada")

    # ==========================================================================
    # CALLBACKS MQTT
    # ==========================================================================

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        """Callback cuando se conecta al broker (paho-mqtt v2)."""
        # paho-mqtt v2: reason_code es un objeto ReasonCode, usar .is_failure
        if not reason_code.is_failure:
            self.is_connected = True
            logger.info(
                f"✅ Conectado al broker MQTT: {self.broker_host}:{self.broker_port} "
                f"(client_id={self.client_id})"
            )

            # Suscribirse a todos los topics del medidor
            self.client.subscribe(self.topic_base, qos=0)
            logger.info(f"📡 Suscrito a: {self.topic_base}")
        else:
            self.is_connected = False
            logger.error(
                f"❌ Error de conexión MQTT: {reason_code} "
                f"(client_id={self.client_id})"
            )

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        """Callback cuando se desconecta del broker (paho-mqtt v2)."""
        self.is_connected = False
        # paho-mqtt v2: reason_code es un objeto ReasonCode, NO usar int()
        if reason_code.is_failure:
            logger.warning(
                f"⚠️ Desconexión inesperada del broker MQTT "
                f"(rc={reason_code}, client_id={self.client_id})"
            )
        else:
            logger.info("🔌 Desconectado del broker MQTT")

    @staticmethod
    def _parse_senml(records: list) -> dict:
        """
        Convierte un array SenML RFC 8428 a un dict plano compatible con la BD.

        El primer registro del array contiene bn (Base Name) y bt (Base Time)
        pero no tiene campo 'n', por lo que se omite en el resultado.

        Mapeo de campos SenML:
            {"n": "vrms",   "u": "V",   "v": 220.5} → {"vrms":   220.5}
            {"n": "online", "vb": True}              → {"online": True}
            {"n": "fw",     "vs": "1.0"} → {"fw": "1.0"}

        Referencia: https://www.rfc-editor.org/rfc/rfc8428
        """
        result = {}
        for record in records:
            name = record.get("n")
            if name is None:
                continue  # Registro base (solo bn/bt), sin campo de valor
            if "v" in record:
                result[name] = record["v"]
            elif "vb" in record:
                result[name] = record["vb"]
            elif "vs" in record:
                result[name] = record["vs"]
        return result

    def _on_message(self, client, userdata, msg):
        """
        Callback cuando se recibe un mensaje MQTT.

        Soporta tres formatos de payload:
          1. SenML RFC 8428  → payload es un JSON array  (list)
          2. JSON plano      → payload es un JSON object (dict)  [legacy]
          3. Texto plano     → "online" / "offline" en topic /conexion
        """
        try:
            topic = msg.topic
            raw_payload = msg.payload.decode("utf-8").strip()

            self.messages_received += 1
            self.last_message_at = datetime.now(timezone.utc)

            logger.info(f"📩 [{topic}]: {raw_payload[:120]}...")

            # Parsear el topic: medidor/{device_id}/{tipo}
            parts = topic.split("/")
            if len(parts) < 3:
                logger.warning(f"⚠️ Topic con formato inválido: {topic}")
                return

            device_id = parts[1]  # ej: "ESP32-001"
            msg_type = parts[2]   # ej: "datos", "estado", "alerta", "conexion"

            # /conexion usa texto plano ("online" / "offline"), no JSON.
            # Se procesa directamente con el raw string.
            if msg_type == "conexion":
                self._process_lwt(device_id, raw_payload)
                return

            # Parsear payload: SenML (list) o JSON plano (dict)
            try:
                parsed = json.loads(raw_payload)
                if isinstance(parsed, list):
                    data = self._parse_senml(parsed)
                    logger.debug(f"📐 SenML: {len(parsed)} registros → {len(data)} campos")
                else:
                    data = parsed  # JSON plano (retrocompatibilidad)
            except json.JSONDecodeError:
                logger.error(f"❌ Payload inválido en topic {topic}: {raw_payload[:80]}")
                return

            # Procesar según tipo de mensaje
            if msg_type == "datos":
                self._process_measurement(device_id, data)
            elif msg_type == "estado":
                self._process_status(device_id, data)
            elif msg_type == "alerta":
                self._process_alert(device_id, data)
            else:
                logger.info(f"ℹ️ Tipo de mensaje no procesado: {msg_type}")

        except Exception as e:
            logger.error(f"❌ Error procesando mensaje: {e}", exc_info=True)

    # ==========================================================================
    # PROCESAMIENTO DE MENSAJES
    # ==========================================================================

    def _process_measurement(self, device_id: str, data: dict):
        """
        Procesa un mensaje de medición de energía y lo guarda en PostgreSQL.

        Formatos soportados del JSON:
        1) Legacy backend:
        {
            "voltage": 120.5,       # Voltaje RMS (V)
            "current": 2.3,         # Corriente RMS (A)
            "power": 277.15,        # Potencia Activa (W)
            "pf": 0.98,             # Factor de potencia
            "frequency": 60.0,      # Frecuencia (Hz)
            "energy": 125.35,       # Energía acumulada (kWh)
            "reactive_power": 25.3, # Potencia Reactiva (VAR)
            "apparent_power": 283.0 # Potencia Aparente (VA)
        }

        2) Firmware ESP32 actual:
        {
            "vrms": 124.06,         # Voltaje RMS (V)
            "irms_a": 0.15,         # Corriente RMS (A)
            "p_w": 18.2,            # Potencia Activa (W)
            "q_var": 2.1,           # Potencia Reactiva (VAR)
            "s_va": 18.3,           # Potencia Aparente (VA)
            "pf": 0.99,             # Factor de potencia
            "f_hz": 59.98,          # Frecuencia (Hz)
            "e_wh": 4.5603          # Energía acumulada (Wh)
        }
        """
        if not self._db_session_factory:
            logger.warning("⚠️ No hay sesión de BD configurada, descartando medición")
            return

        try:
            # Importar aquí para evitar imports circulares
            from app.models.medicion import Medicion
            from app.models.dispositivo import Dispositivo

            def _get_first_float(keys: tuple[str, ...], default: float) -> float:
                for key in keys:
                    if key in data and data[key] is not None:
                        try:
                            return float(data[key])
                        except (TypeError, ValueError):
                            continue
                return default

            # Soporte dual: SenML RFC 8428 (primer alias) + JSON plano legacy
            # SenML /datos: vrms, irms, p_act, p_react, p_ap, pf, freq, e_act, temp, pq
            # JSON legacy:  vrms/voltage, irms_a/current, p_w/power, q_var, s_va, f_hz, e_wh
            voltage        = _get_first_float(("vrms", "voltage"),              0.0)
            current        = _get_first_float(("irms", "irms_a", "current"),    0.0)
            active_power   = _get_first_float(("p_act", "p_w", "power"),        0.0)
            reactive_power = _get_first_float(("p_react", "q_var",
                                               "reactive_power"),               0.0)
            apparent_power = _get_first_float(("p_ap", "s_va",
                                               "apparent_power"),               0.0)
            power_factor   = _get_first_float(("pf",),                          1.0)
            frequency      = _get_first_float(("freq", "f_hz", "frequency"),   60.0)
            active_energy  = _get_first_float(("e_act", "e_wh", "energy"),      0.0)
            reactive_energy = _get_first_float(("e_varh", "reactive_energy"),   0.0)

            db = self._db_session_factory()
            try:
                # Buscar o crear dispositivo
                dispositivo = db.query(Dispositivo).filter(
                    Dispositivo.device_id == device_id
                ).first()

                if not dispositivo:
                    # Auto-registrar dispositivo
                    dispositivo = Dispositivo(
                        device_id=device_id,
                        nombre=f"Auto-registrado: {device_id}",
                        activo=True,
                    )
                    db.add(dispositivo)
                    db.commit()
                    db.refresh(dispositivo)
                    logger.info(f"🆕 Dispositivo auto-registrado: {device_id}")

                # Actualizar estado de conexión del dispositivo
                dispositivo.conectado = True
                dispositivo.ultima_conexion = datetime.now(timezone.utc)

                # Crear medición mapeando campos del JSON a campos de la BD
                medicion = Medicion(
                    dispositivo_id=dispositivo.id,
                    device_id=device_id,
                    voltaje_rms=voltage,
                    corriente_rms=current,
                    potencia_activa=active_power,
                    potencia_reactiva=reactive_power,
                    potencia_aparente=apparent_power,
                    factor_potencia=power_factor,
                    frecuencia=frequency,
                    energia_activa=active_energy,
                    energia_reactiva=reactive_energy,
                    timestamp=datetime.now(timezone.utc),
                )

                db.add(medicion)
                db.commit()

                self.messages_saved += 1
                logger.info(
                    f"💾 Medición guardada: {device_id} | "
                    f"V={voltage:.1f}V | "
                    f"I={current:.2f}A | "
                    f"P={active_power:.1f}W | "
                    f"PF={power_factor:.2f}"
                )

                # Auto-generar alertas por voltaje según CREG 024/2015
                # Nominal 120V, tolerancia ±10% → rango 108V - 132V
                VOLTAJE_NOMINAL = 120.0
                VOLTAJE_MIN = VOLTAJE_NOMINAL * 0.9   # 108V
                VOLTAJE_MAX = VOLTAJE_NOMINAL * 1.1   # 132V

                if voltage > 0 and (voltage > VOLTAJE_MAX or voltage < VOLTAJE_MIN):
                    from app.models.evento import Evento

                    if voltage > VOLTAJE_MAX:
                        tipo = "sobrevoltaje"
                        mensaje = f"Sobrevoltaje detectado: {voltage:.1f}V (límite CREG: {VOLTAJE_MAX:.0f}V)"
                        umbral = VOLTAJE_MAX
                    else:
                        tipo = "subtension"
                        mensaje = f"Subtensión detectada: {voltage:.1f}V (límite CREG: {VOLTAJE_MIN:.0f}V)"
                        umbral = VOLTAJE_MIN

                    evento = Evento(
                        device_id=device_id,
                        tipo=tipo,
                        severidad="critical",
                        valor=voltage,
                        umbral=umbral,
                        mensaje=mensaje,
                        activo=True,
                    )
                    db.add(evento)
                    db.commit()
                    logger.warning(f"🚨 Alerta CREG auto-generada: {device_id} | {mensaje}")

            finally:
                db.close()

        except Exception as e:
            logger.error(f"❌ Error guardando medición: {e}", exc_info=True)

    def _process_status(self, device_id: str, data: dict):
        """
        Procesa un mensaje del topic /estado (salud del nodo).

        Mapeo SenML RFC 8428 del firmware -> columnas NodoSalud:

            online       (vb)    -> online
            ac           (vb)    -> ac_ok
            carga        (vb)    -> carga
            ade          (vb)    -> ade_ok
            red_cel      (vb)    -> modem_ok
            mqtt         (vb)    -> mqtt_ok
            cal_ok       (vb)    -> cal_ok
            ade_perd     (v)     -> ade_perdidas
            ade_rec_ok   (v)     -> ade_rec         (recuperaciones exitosas)
            red_int      (v)     -> red_intentos
            red_ok       (v)     -> red_exitos      (=reconexiones)
            mqtt_int     (v)     -> mqtt_intentos
            mqtt_ok_cnt  (v)     -> mqtt_exitos     (=msg_tx)
            rssi_dbm     (v)     -> rssi_dbm
            fw           (vs)    -> fw_version

        Acepta nombres legacy (ac_ok, modem_ok, mqtt_ok, ade_ok, ade_rec,
        msg_tx, reconexiones, firmware) para retrocompat. sin penalizar.
        """
        if not self._db_session_factory:
            return

        try:
            from app.models.dispositivo import Dispositivo
            from app.models.nodo_salud import NodoSalud

            db = self._db_session_factory()
            try:
                dispositivo = db.query(Dispositivo).filter(
                    Dispositivo.device_id == device_id
                ).first()

                if not dispositivo:
                    logger.warning(f"⚠️ /estado: dispositivo '{device_id}' no encontrado en BD")
                    return

                # Estado de conexion: campo SenML "online" o true por defecto
                online = data.get("online", True)
                dispositivo.conectado = bool(online)
                dispositivo.ultima_conexion = datetime.now(timezone.utc)

                # ── Helpers tolerantes ──────────────────────────────────────
                def _bool_any(*keys):
                    """Primer key con valor no-null, convertido a bool."""
                    for k in keys:
                        if k in data and data[k] is not None:
                            return bool(data[k])
                    return None

                def _int_any(*keys):
                    for k in keys:
                        if k in data and data[k] is not None:
                            try:
                                return int(data[k])
                            except (TypeError, ValueError):
                                continue
                    return None

                def _float_any(*keys):
                    for k in keys:
                        if k in data and data[k] is not None:
                            try:
                                return float(data[k])
                            except (TypeError, ValueError):
                                continue
                    return None

                def _str_any(*keys):
                    for k in keys:
                        if k in data and data[k] is not None:
                            return str(data[k])
                    return None

                # Firmware: "fw" (SenML). Legacy: "firmware".
                fw_version = _str_any("fw", "firmware")
                if fw_version:
                    dispositivo.firmware_version = fw_version

                # Mapear SenML firmware -> columnas BD, con fallback legacy.
                modem_ok    = _bool_any("red_cel", "modem_ok")
                mqtt_ok_val = _bool_any("mqtt", "mqtt_ok")
                ade_ok_val  = _bool_any("ade", "ade_ok")
                ac_ok       = _bool_any("ac", "ac_ok")
                carga       = _bool_any("carga")
                cal_ok      = _bool_any("cal_ok")
                ade_bus     = _bool_any("ade_bus")

                ade_perdidas  = _int_any("ade_perd", "ade_perdidas")
                ade_rec       = _int_any("ade_rec_ok", "ade_rec")
                red_intentos  = _int_any("red_int", "red_intentos")
                red_exitos    = _int_any("red_ok", "red_exitos", "reconexiones")
                mqtt_intentos = _int_any("mqtt_int", "mqtt_intentos")
                mqtt_exitos   = _int_any("mqtt_ok_cnt", "mqtt_exitos", "msg_tx")

                rssi_dbm = _float_any("rssi_dbm", "rssi")
                imei     = _str_any("imei")

                nodo_salud = NodoSalud(
                    device_id=device_id,
                    online=bool(online),
                    ac_ok=ac_ok,
                    carga=carga,
                    ade_ok=ade_ok_val,
                    ade_bus=ade_bus,
                    ade_rec=ade_rec,
                    modem_ok=modem_ok,
                    mqtt_ok=mqtt_ok_val,
                    cal_ok=cal_ok,
                    rssi_dbm=rssi_dbm,
                    # Compatibilidad: msg_tx = mqtt_exitos, reconexiones = red_exitos.
                    msg_tx=mqtt_exitos,
                    reconexiones=red_exitos,
                    ade_perdidas=ade_perdidas,
                    red_intentos=red_intentos,
                    red_exitos=red_exitos,
                    mqtt_intentos=mqtt_intentos,
                    mqtt_exitos=mqtt_exitos,
                    fw_version=fw_version,
                    imei=imei,
                    timestamp=datetime.now(timezone.utc),
                )

                db.add(nodo_salud)
                db.commit()

                logger.info(
                    f"📊 /estado {device_id} | "
                    f"ac={ac_ok} carga={carga} ade={ade_ok_val} "
                    f"modem={modem_ok} mqtt={mqtt_ok_val} cal={cal_ok} "
                    f"rssi={rssi_dbm} pkt_ok={mqtt_exitos} recon={red_exitos}"
                )

            finally:
                db.close()

        except Exception as e:
            logger.error(f"❌ Error procesando /estado: {e}", exc_info=True)

    def _process_lwt(self, device_id: str, raw: str):
        """
        Procesa el topic /conexion: indicador LWT online/offline.

        El firmware publica "online" al establecer sesion MQTT (retain=1).
        El broker publica "offline" automaticamente si el nodo cae sin
        desconectarse (Last Will and Testament configurado via AT+SMCONF).

        raw: "online" o "offline" (texto plano, sin JSON).
        """
        if not self._db_session_factory:
            return

        online = raw.lower() == "online"

        try:
            from app.models.dispositivo import Dispositivo

            db = self._db_session_factory()
            try:
                dispositivo = db.query(Dispositivo).filter(
                    Dispositivo.device_id == device_id
                ).first()

                if dispositivo:
                    dispositivo.conectado = online
                    if online:
                        dispositivo.ultima_conexion = datetime.now(timezone.utc)
                    db.commit()
                    estado_str = "🟢 ONLINE" if online else "🔴 OFFLINE"
                    logger.info(f"📡 /conexion {device_id}: {estado_str}")
                else:
                    logger.warning(
                        f"⚠️ /conexion: dispositivo '{device_id}' no encontrado en BD"
                    )

            finally:
                db.close()

        except Exception as e:
            logger.error(f"❌ Error procesando /conexion: {e}", exc_info=True)

    def _process_alert(self, device_id: str, data: dict):
        """
        Procesa un mensaje del topic /alerta y lo persiste en BD.

        Formato SenML RFC 8428 del firmware (activo):
            {"tipo": "sag"|"swell"|"freq_oor"|"overtemp"|"pq_event",
             "vrms": 220.5, "pq": 4}

        Formato legacy JSON plano (retrocompatibilidad):
            {"type": "overvoltage", "value": 135.5, "threshold": 130.0,
             "message": "..."}
        """
        # Soporte dual SenML ("tipo") + legacy ("type")
        tipo_raw = data.get("tipo") or data.get("type") or ""
        valor = data.get("vrms", data.get("value"))
        pq_flags = data.get("pq")
        umbral = data.get("threshold")
        mensaje_in = data.get("message")

        logger.warning(
            f"🚨 ALERTA de {device_id}: "
            f"tipo={tipo_raw or 'unknown'} | "
            f"valor={valor if valor is not None else 'N/A'} | "
            f"pq={pq_flags if pq_flags is not None else 'N/A'}"
        )

        if not self._db_session_factory:
            return

        try:
            from app.models.evento import Evento

            # Mapeo: etiquetas firmware (SenML) + legacy EN -> taxonomia backend.
            tipo_map = {
                # Firmware SenML
                "sag": "subtension",
                "swell": "sobrevoltaje",
                "freq_oor": "frecuencia_fuera_rango",
                "overtemp": "sobretemperatura",
                "pq_event": "alerta_pq",
                # Legacy
                "overvoltage": "sobrevoltaje",
                "undervoltage": "subtension",
                "overcurrent": "sobrecorriente",
                "power_loss": "perdida_energia",
                "communication": "comunicacion_debil",
            }
            tipo = tipo_map.get(tipo_raw, tipo_raw or "alerta_firmware")

            severidad_critical = {"sobrevoltaje", "subtension", "sobretemperatura"}
            severidad = "critical" if tipo in severidad_critical else "warning"

            # Mensaje legible: usa el mensaje del payload si viene, si no, construye uno.
            if mensaje_in:
                mensaje = mensaje_in
            elif valor is not None:
                mensaje = f"Alerta {tipo}: Vrms={valor:.1f}V"
            else:
                mensaje = f"Alerta: {tipo}"

            db = self._db_session_factory()
            try:
                evento = Evento(
                    device_id=device_id,
                    tipo=tipo,
                    severidad=severidad,
                    valor=valor,
                    umbral=umbral,
                    mensaje=mensaje,
                    activo=True,
                )
                db.add(evento)
                db.commit()
                logger.info(
                    f"💾 Alerta guardada en BD: {device_id} | {tipo} | "
                    f"valor={valor} pq={pq_flags}"
                )
            finally:
                db.close()

        except Exception as e:
            logger.error(f"❌ Error guardando alerta: {e}", exc_info=True)

    # ==========================================================================
    # CONTROL DEL CLIENTE
    # ==========================================================================

    def start(self):
        """Inicia la conexión MQTT en un hilo separado."""
        self._should_stop = False

        def _run():
            while not self._should_stop:
                try:
                    logger.info(
                        f"🔄 Conectando a MQTT: {self.broker_host}:{self.broker_port} "
                        f"(client_id={self.client_id})"
                    )
                    self.client.connect(self.broker_host, self.broker_port, keepalive=60)
                    self.client.loop_forever()
                except Exception as e:
                    logger.error(f"❌ Error en conexión MQTT: {e}")
                    if not self._should_stop:
                        logger.info("⏳ Reintentando en 10 segundos...")
                        time.sleep(10)

        self._thread = threading.Thread(target=_run, daemon=True)
        self._thread.start()
        logger.info("🚀 Cliente MQTT iniciado en hilo secundario")

    def stop(self):
        """Detiene la conexión MQTT."""
        self._should_stop = True
        self.client.disconnect()
        if self._thread:
            self._thread.join(timeout=5)
        logger.info("🛑 Cliente MQTT detenido")

    def get_status(self) -> dict:
        """Retorna el estado actual del cliente MQTT."""
        return {
            "connected": self.is_connected,
            "broker": f"{self.broker_host}:{self.broker_port}",
            "topic": self.topic_base,
            "messages_received": self.messages_received,
            "messages_saved": self.messages_saved,
            "last_message_at": (
                self.last_message_at.isoformat() if self.last_message_at else None
            ),
        }


# ==========================================================================
# INSTANCIA GLOBAL (Singleton)
# ==========================================================================

# Esta instancia se importa desde main.py y otros módulos
mqtt_client: Optional[MQTTClient] = None


def create_mqtt_client(
    broker_host: str,
    broker_port: int,
    username: str,
    password: str,
    topic_base: str = "medidor/#",
    client_id: str = "",
    use_tls: bool = False,
) -> MQTTClient:
    """Crea y retorna la instancia global del cliente MQTT."""
    global mqtt_client
    mqtt_client = MQTTClient(
        broker_host=broker_host,
        broker_port=broker_port,
        username=username,
        password=password,
        topic_base=topic_base,
        client_id=client_id,
        use_tls=use_tls,
    )
    return mqtt_client


def get_mqtt_client() -> Optional[MQTTClient]:
    """Retorna la instancia global del cliente MQTT."""
    return mqtt_client
