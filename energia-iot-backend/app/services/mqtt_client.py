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
from sqlalchemy.exc import IntegrityError
from datetime import datetime, timezone
from typing import Optional

import paho.mqtt.client as mqtt

from app.config import settings
from app.services.telemetry_contract import trace_metadata

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
        tls_ca_file: str = "",
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

        # TLS: si se entrega la CA propia del proyecto se ancla (pinning) —
        # necesaria porque el certificado del broker lo emite nuestra CA y
        # no una CA pública. Sin ruta, se validan las CAs del sistema.
        if self.use_tls:
            self.client.tls_set(
                ca_certs=tls_ca_file or None,
                cert_reqs=ssl.CERT_REQUIRED,
                tls_version=ssl.PROTOCOL_TLS_CLIENT,
            )
            logger.info(
                "TLS habilitado para conexión MQTT (CA %s)",
                "propia anclada" if tls_ca_file else "del sistema",
            )

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
        logger.info("Fábrica de sesiones de BD configurada")

    # ==========================================================================
    # CALLBACKS MQTT
    # ==========================================================================

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        """Callback cuando se conecta al broker (paho-mqtt v2)."""
        # paho-mqtt v2: reason_code es un objeto ReasonCode, usar .is_failure
        if not reason_code.is_failure:
            self.is_connected = True
            logger.info(
                f"Conectado al broker MQTT: {self.broker_host}:{self.broker_port} "
                f"(client_id={self.client_id})"
            )

            # Suscribirse a todos los topics del medidor
            self.client.subscribe(self.topic_base, qos=0)
            logger.info(f"Suscrito a: {self.topic_base}")
        else:
            self.is_connected = False
            logger.error(
                f"Error de conexión MQTT: {reason_code} "
                f"(client_id={self.client_id})"
            )

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        """Callback cuando se desconecta del broker (paho-mqtt v2)."""
        self.is_connected = False
        # paho-mqtt v2: reason_code es un objeto ReasonCode, NO usar int()
        if reason_code.is_failure:
            logger.warning(
                f"Desconexión inesperada del broker MQTT "
                f"(rc={reason_code}, client_id={self.client_id})"
            )
        else:
            logger.info("Desconectado del broker MQTT")

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
                # Preserve UTC acquisition time and the declared device ID.
                # Legacy packets may still have bt=0 and no trace identity.
                if "bt" in record:
                    result["_bt"] = record["bt"]
                if "bn" in record and record["bn"].startswith("urn:dev:serial:"):
                    result["_device_id"] = record["bn"].removeprefix("urn:dev:serial:").removesuffix(":/")
                continue
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
          3. Texto plano     → presencia legacy en topic /conexion
        """
        try:
            received_at = datetime.now(timezone.utc)
            topic = msg.topic
            raw_payload = msg.payload.decode("utf-8").strip()

            self.messages_received += 1
            self.last_message_at = datetime.now(timezone.utc)

            logger.info(f"[{topic}]: {raw_payload[:120]}...")

            # Parsear el topic: medidor/{device_id}/{tipo}
            parts = topic.split("/")
            if len(parts) < 3:
                logger.warning(f"Topic con formato inválido: {topic}")
                return

            device_id = parts[1]  # ej: "ESP32-001"
            msg_type = parts[2]   # ej: "datos", "estado", "alerta", "conexion"

            # Mensajes retained: el broker los reenvía al (re)suscribirse,
            # pero pueden ser de hace días (p. ej. un /estado "online" de un
            # nodo que murió después). Tomarlos como tráfico fresco revivía
            # medidores apagados en cada reinicio del backend, así que solo
            # cuentan para conexión los mensajes en vivo.
            es_retained = bool(getattr(msg, "retain", False))

            # /conexion usa texto plano ("online" / "offline"), no JSON.
            # Se procesa directamente con el raw string.
            if msg_type == "conexion":
                if es_retained:
                    logger.debug(f"/conexion retained de {device_id} ignorado")
                    return
                self._process_legacy_presence(device_id, raw_payload)
                return

            # Parsear payload: SenML (list) o JSON plano (dict)
            try:
                parsed = json.loads(raw_payload)
                if isinstance(parsed, list):
                    data = self._parse_senml(parsed)
                    logger.debug(f"SenML: {len(parsed)} registros → {len(data)} campos")
                else:
                    data = parsed  # JSON plano (retrocompatibilidad)
            except json.JSONDecodeError:
                logger.error(f"Payload inválido en topic {topic}: {raw_payload[:80]}")
                return

            if not isinstance(data, dict):
                logger.warning("Payload must be a SenML pack or JSON object")
                return
            data["_raw_payload"] = raw_payload
            if data.get("_device_id", device_id).upper() != device_id.upper():
                logger.error("SenML device identity does not match topic %s", topic)
                return

            # Procesar según tipo de mensaje
            if msg_type == "datos":
                self._process_measurement(device_id, data, received_at=received_at)
            elif msg_type == "estado":
                self._process_status(device_id, data, es_retained=es_retained)
            elif msg_type == "alerta":
                self._process_alert(device_id, data)
            else:
                logger.info(f"Tipo de mensaje no procesado: {msg_type}")

        except Exception as e:
            logger.error(f"Error procesando mensaje: {e}", exc_info=True)

    # ==========================================================================
    # PROCESAMIENTO DE MENSAJES
    # ==========================================================================

    def _process_measurement(self, device_id: str, data: dict, received_at=None):
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
            logger.warning("No hay sesión de BD configurada, descartando medición")
            return

        try:
            trace = trace_metadata(data, received_at)
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

                # Un medidor retirado desde la plataforma (activo=False) que
                # vuelve a reportar se reactiva solo: su histórico nunca se
                # borró, así que la serie continúa donde quedó.
                if not dispositivo.activo:
                    dispositivo.activo = True
                    logger.info(f"Dispositivo {device_id} reactivado: volvió a publicar mediciones")

                # Actualizar estado de conexión del dispositivo
                dispositivo.conectado = True
                dispositivo.ultima_conexion = trace["ts_server"]

                # Timestamp: por defecto la hora de recepción. Si el registro
                # base SenML trae bt absoluto (> 2^28 según RFC 8428 sec 4.5.3)
                # es una medición diferida del backlog SD del nodo y se respeta
                # su hora original.
                ts_medicion = trace["ts_device"] or trace["ts_server"]
                identity = None
                if trace["boot_id"] is not None:
                    identity = (Medicion.device_id == device_id,
                        Medicion.boot_id == trace["boot_id"], Medicion.seq == trace["seq"])
                    existing = db.query(Medicion).filter(*identity).first()
                    if existing is not None:
                        self._save_duplicate(db, existing, trace)
                        return

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
                    timestamp=ts_medicion,
                    **trace,
                )

                db.add(medicion)
                try:
                    db.commit()
                except IntegrityError:
                    db.rollback()
                    # Multiple backend consumers may race on the same packet.
                    existing = db.query(Medicion).filter(*identity).first() if identity else None
                    if existing is None:
                        raise
                    self._save_duplicate(db, existing, trace)
                    return

                self.messages_saved += 1
                logger.info(
                    f"Medición guardada: {device_id} | "
                    f"V={voltage:.1f}V | "
                    f"I={current:.2f}A | "
                    f"P={active_power:.1f}W | "
                    f"PF={power_factor:.2f}"
                )

                # Auto-generar alertas por voltaje según CREG 024/2015.
                # Umbrales centralizados en config (110 V ±10% → 99–121 V por
                # defecto, ajustables por variable de entorno según la zona).
                VOLTAJE_MIN = settings.voltaje_min
                VOLTAJE_MAX = settings.voltaje_max

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
                    logger.warning(f"Alerta CREG auto-generada: {device_id} | {mensaje}")

            finally:
                db.close()

        except Exception as e:
            logger.error(f"Error guardando medición: {e}", exc_info=True)

    @staticmethod
    def _save_duplicate(db, existing, trace):
        from app.models.medicion import Medicion
        conflict = existing.payload_sha256 != trace["payload_sha256"]
        db.query(Medicion).filter(Medicion.id == existing.id).update({
            Medicion.duplicate_count: Medicion.duplicate_count + 1,
            Medicion.identity_conflict_count: Medicion.identity_conflict_count + int(conflict),
            Medicion.last_received_at: trace["ts_server"],
        }, synchronize_session=False)
        db.commit()
        if conflict:
            logger.error("Conflicting payload for %s boot=%s seq=%s",
                         existing.device_id, existing.boot_id, existing.seq)

    def _process_status(self, device_id: str, data: dict, es_retained: bool = False):
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
            mqtt_ok_cnt  (v)     -> mqtt_exitos (sesiones; alias legacy)
            data_publish_ok     -> msg_tx (aceptacion por el modem)
            reconnect_count     -> reconexiones (sin sesion inicial)
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
                    logger.warning(f"/estado: dispositivo '{device_id}' no encontrado en BD")
                    return

                # Estado de conexion: campo SenML "online" o true por defecto.
                # Un /estado retained solo aporta datos de salud: su "online"
                # y su marca de tiempo pertenecen al pasado.
                online = data.get("online", True)
                if not es_retained:
                    if not dispositivo.activo and online:
                        dispositivo.activo = True
                        logger.info(f"Dispositivo {device_id} reactivado: reporta /estado")
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
                mqtt_exitos   = _int_any("mqtt_sessions_ok", "mqtt_ok_cnt", "mqtt_exitos")
                msg_tx = _int_any("data_publish_ok", "msg_tx")
                reconexiones = _int_any("reconnect_count", "reconexiones")
                clock = trace_metadata({k: data[k] for k in
                    ("ts_device", "time_valid") if k in data})
                radio_valid = _bool_any("radio_valid")

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
                    msg_tx=msg_tx,
                    reconexiones=reconexiones,
                    boot_id=_int_any("boot_id"),
                    uptime_ms=_int_any("uptime_ms"),
                    ts_device=clock["ts_device"],
                    time_valid=clock["time_valid"],
                    data_generated_count=_int_any("data_generated_count"),
                    data_publish_attempts=_int_any("data_publish_attempts"),
                    data_publish_ok=_int_any("data_publish_ok"),
                    ac_loss_count=_int_any("ac_loss_count"),
                    ac_restore_count=_int_any("ac_restore_count"),
                    reset_reason=_int_any("reset_reason"),
                    reset_reason_name=_str_any("reset_reason_name"),
                    radio_valid=radio_valid,
                    rsrp_dbm=_float_any("rsrp_dbm") if radio_valid else None,
                    rsrq_db=_float_any("rsrq_db") if radio_valid else None,
                    rssnr_raw=_float_any("rssnr_raw") if radio_valid else None,
                    sinr_db=_float_any("sinr_db") if radio_valid else None,
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
                    f"/estado {device_id} | "
                    f"ac={ac_ok} carga={carga} ade={ade_ok_val} "
                    f"modem={modem_ok} mqtt={mqtt_ok_val} cal={cal_ok} "
                    f"rssi={rssi_dbm} data_ok={msg_tx} mqtt_sessions={mqtt_exitos} recon={reconexiones}"
                )

            finally:
                db.close()

        except Exception as e:
            logger.error(f"Error procesando /estado: {e}", exc_info=True)

    def _process_legacy_presence(self, device_id: str, raw: str):
        """
        Compatibilidad con firmware anterior que publicaba /conexion.

        No es la fuente de verdad: la expiracion por `ultima_conexion` corrige
        cualquier estado obsoleto y el firmware actual ya no configura LWT.

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
                    estado_str = "ONLINE" if online else "OFFLINE"
                    logger.info(f"/conexion {device_id}: {estado_str}")
                else:
                    logger.warning(
                        f"/conexion: dispositivo '{device_id}' no encontrado en BD"
                    )

            finally:
                db.close()

        except Exception as e:
            logger.error(f"Error procesando /conexion: {e}", exc_info=True)

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
        # Soporte de eventos de salud (code) y alertas PQ/legacy.
        tipo_raw = data.get("code") or data.get("tipo") or data.get("type") or ""
        tipo_raw = str(tipo_raw).strip().lower()
        valor = data.get("vrms", data.get("value"))
        pq_flags = data.get("pq")
        umbral = data.get("threshold")
        mensaje_in = data.get("message")

        logger.warning(
            f"ALERTA de {device_id}: "
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
            severity_in = str(data.get("severity") or "").strip().lower()
            severidad = (severity_in if severity_in in {"info", "warning", "critical"}
                         else ("critical" if tipo in severidad_critical else "warning"))
            transition = str(data.get("transition") or "").strip().lower() or None
            event_id = str(data.get("event_id") or "").strip() or None
            module = str(data.get("module") or "").strip().lower() or None
            code = str(data.get("code") or "").strip().lower() or None

            try:
                boot_id = int(data["boot_id"]) if data.get("boot_id") is not None else None
                seq = int(data["seq"]) if data.get("seq") is not None else None
                uptime_ms = int(data["uptime_ms"]) if data.get("uptime_ms") is not None else None
                occurrence_count = int(data["count"]) if data.get("count") is not None else None
                error = int(data["error"]) if data.get("error") is not None else None
            except (TypeError, ValueError, OverflowError):
                boot_id = seq = uptime_ms = occurrence_count = error = None

            clock_data = {k: data[k] for k in
                          ("boot_id", "seq", "ts_device", "time_valid") if k in data}
            try:
                event_clock = trace_metadata(clock_data)
            except ValueError:
                event_clock = {"ts_device": None, "time_valid": False}

            # Los eventos informativos y de recuperacion quedan en el historial,
            # pero no deben inflar el contador de alarmas activas.
            activo = not (transition == "cleared" or severidad == "info" or
                          tipo in {"ac_restored", "time_synced", "module_ready"})

            # Mensaje legible: usa el mensaje del payload si viene, si no, construye uno.
            if mensaje_in:
                mensaje = mensaje_in
            elif code:
                mensaje = (f"{code} module={module or 'unknown'} "
                           f"transition={transition or 'occurrence'}")
            elif valor is not None:
                mensaje = f"Alerta {tipo}: Vrms={valor:.1f}V"
            else:
                mensaje = f"Alerta: {tipo}"

            db = self._db_session_factory()
            try:
                if event_id:
                    existing = (db.query(Evento)
                        .filter(Evento.device_id == device_id,
                                Evento.event_id == event_id).first())
                    if existing is not None:
                        logger.info("Evento duplicado ignorado: %s %s", device_id, event_id)
                        return

                # Una transicion CLEARED resuelve su alarma previa. AC_RESTORED
                # resuelve especificamente AC_LOST sin borrar la evidencia.
                resolve_tipo = ("ac_lost" if tipo == "ac_restored"
                                else tipo if transition == "cleared" else None)
                if resolve_tipo:
                    (db.query(Evento)
                       .filter(Evento.device_id == device_id,
                               Evento.tipo == resolve_tipo,
                               Evento.activo.is_(True))
                       .update({Evento.activo: False}, synchronize_session=False))

                evento = Evento(
                    device_id=device_id,
                    tipo=tipo,
                    severidad=severidad,
                    valor=valor,
                    umbral=umbral,
                    mensaje=mensaje,
                    activo=activo,
                    event_id=event_id,
                    module=module,
                    code=code,
                    transition=transition,
                    boot_id=boot_id,
                    seq=seq,
                    uptime_ms=uptime_ms,
                    occurrence_count=occurrence_count,
                    error=error,
                    fw_version=str(data.get("fw"))[:20] if data.get("fw") else None,
                    time_valid=event_clock.get("time_valid"),
                    ts_device=event_clock.get("ts_device"),
                )
                db.add(evento)
                db.commit()
                logger.info(
                    f"Alerta guardada en BD: {device_id} | {tipo} | "
                    f"valor={valor} pq={pq_flags}"
                )
            finally:
                db.close()

        except Exception as e:
            logger.error(f"Error guardando alerta: {e}", exc_info=True)

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
                        f"Conectando a MQTT: {self.broker_host}:{self.broker_port} "
                        f"(client_id={self.client_id})"
                    )
                    self.client.connect(self.broker_host, self.broker_port, keepalive=60)
                    self.client.loop_forever()
                except Exception as e:
                    logger.error(f"Error en conexión MQTT: {e}")
                    if not self._should_stop:
                        logger.info("⏳ Reintentando en 10 segundos...")
                        time.sleep(10)

        self._thread = threading.Thread(target=_run, daemon=True)
        self._thread.start()
        logger.info("Cliente MQTT iniciado en hilo secundario")

    def stop(self):
        """Detiene la conexión MQTT."""
        self._should_stop = True
        self.client.disconnect()
        if self._thread:
            self._thread.join(timeout=5)
        logger.info("Cliente MQTT detenido")

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
    tls_ca_file: str = "",
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
        tls_ca_file=tls_ca_file,
    )
    return mqtt_client


def get_mqtt_client() -> Optional[MQTTClient]:
    """Retorna la instancia global del cliente MQTT."""
    return mqtt_client
