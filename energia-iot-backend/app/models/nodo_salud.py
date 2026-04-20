"""MODELO: NodoSalud - Historial de salud y métricas de transmisión del nodo ESP32"""
from sqlalchemy import Column, Integer, String, Boolean, DateTime, Float, Index
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from app.database import Base


class NodoSalud(Base):
    __tablename__ = "nodo_salud"

    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String(50), nullable=False, index=True)

    # ── Estado lógico del nodo ─────────────────────────────────────────────
    online    = Column(Boolean, nullable=True)   # Nodo reporta online
    ac_ok     = Column(Boolean, nullable=True)   # Línea AC presente (vrms > umbral)
    carga     = Column(Boolean, nullable=True)   # Carga detectada en el circuito
    ade_ok    = Column(Boolean, nullable=True)   # ADE9153A respondiendo + bus SPI OK
    ade_bus   = Column(Boolean, nullable=True)   # bus_disconnected del ADE (false=OK)
    ade_rec   = Column(Integer, nullable=True)   # Contador de recuperaciones del ADE

    # ── Estado de comunicación ─────────────────────────────────────────────
    modem_ok  = Column(Boolean, nullable=True)   # SIM7080G responde AT
    mqtt_ok   = Column(Boolean, nullable=True)   # Sesión MQTT activa en el nodo
    cal_ok    = Column(Boolean, nullable=True)   # Calibración mSure válida y cargada

    # ── Métricas de transmisión ───────────────────────────────────────────
    rssi_dbm      = Column(Float,   nullable=True)  # Señal celular (dBm), -127 = N/A
    msg_tx        = Column(Integer, nullable=True)  # Mensajes /datos publicados (=mqtt_exitos)
    reconexiones  = Column(Integer, nullable=True)  # Sesiones MQTT establecidas (=red_exitos)

    # ── Contadores de confiabilidad (desde arranque del firmware) ─────────
    ade_perdidas    = Column(Integer, nullable=True)  # Veces que el ADE perdió comunicación
    red_intentos    = Column(Integer, nullable=True)  # Intentos de conexión celular
    red_exitos      = Column(Integer, nullable=True)  # Conexiones celulares exitosas
    mqtt_intentos   = Column(Integer, nullable=True)  # Intentos de sesión MQTT
    mqtt_exitos     = Column(Integer, nullable=True)  # Sesiones MQTT exitosas (=msg_tx en modelo nuevo)

    # ── Identificación de firmware y hardware ─────────────────────────────
    fw_version = Column(String(20), nullable=True)
    imei       = Column(String(20), nullable=True)  # IMEI del SIM7080G (UID único del hardware)

    # ── Timestamps ────────────────────────────────────────────────────────
    timestamp  = Column(DateTime(timezone=True), nullable=False, index=True)
    created_at = Column(DateTime(timezone=True), server_default=func.now())

    __table_args__ = (
        Index("idx_nodo_salud_device_ts", "device_id", "timestamp"),
    )

    def __repr__(self) -> str:
        return f"<NodoSalud(device_id='{self.device_id}', ts={self.timestamp})>"
