"""ROUTER: Admin - Diagnóstico y seed de base de datos"""
from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from sqlalchemy import text, inspect
from datetime import datetime, timezone

from app.database import get_db, get_engine
from app.models.dispositivo import Dispositivo
from app.models.medicion import Medicion
from app.models.nodo_salud import NodoSalud
from app.models.usuario import Usuario
from app.models.evento import Evento
from app.models.audit_log import AuditLog
from app.services.auth import get_current_user

router = APIRouter(prefix="/admin", tags=["Admin"])


@router.get("/diagnostico")
def diagnostico_bd(db: Session = Depends(get_db)):
    """
    Diagnóstico completo de la base de datos.
    Muestra tablas existentes, conteo de registros y estado general.
    """
    engine = get_engine()
    inspector = inspect(engine)

    # Tablas existentes
    tablas = inspector.get_table_names()

    # Conteos
    conteos = {}
    modelos = {
        "dispositivos": Dispositivo,
        "mediciones": Medicion,
        "nodo_salud": NodoSalud,
        "usuarios": Usuario,
        "eventos": Evento,
        "audit_log": AuditLog,
    }

    for nombre, modelo in modelos.items():
        try:
            conteos[nombre] = db.query(modelo).count()
        except Exception as e:
            conteos[nombre] = f"Error: {str(e)}"

    # Info de dispositivos existentes
    dispositivos = []
    try:
        devs = db.query(Dispositivo).all()
        for d in devs:
            dispositivos.append({
                "id": d.id,
                "device_id": d.device_id,
                "nombre": d.nombre,
                "ubicacion": d.ubicacion,
                "conectado": d.conectado,
                "ultima_conexion": d.ultima_conexion.isoformat() if d.ultima_conexion else None,
                "created_at": d.created_at.isoformat() if d.created_at else None,
            })
    except Exception as e:
        dispositivos = [{"error": str(e)}]

    # Info de conexión
    db_url = str(engine.url)
    # Enmascarar contraseña
    if "@" in db_url:
        parts = db_url.split("@")
        prefix = parts[0].rsplit(":", 1)[0]
        db_url = f"{prefix}:***@{parts[1]}"

    return {
        "database_url": db_url,
        "tablas_existentes": tablas,
        "conteo_registros": conteos,
        "dispositivos": dispositivos,
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }


@router.post("/seed")
def seed_dispositivos(db: Session = Depends(get_db)):
    """
    Crea los dispositivos iniciales del sistema de medición.
    Solo crea los que no existen (idempotente).
    """
    dispositivos_seed = [
        {
            "device_id": "ESP32-001",
            "nombre": "Medidor Principal - Lab IoT",
            "ubicacion": "Laboratorio IoT - Edificio 67",
            "descripcion": "Medidor principal del laboratorio de IoT, conectado al tablero eléctrico principal. ADE9153A + SIM7080G.",
            "firmware_version": "1.0.0",
            "intervalo_medicion": 60,
        },
        {
            "device_id": "ESP32-002",
            "nombre": "Medidor Secundario - Oficina",
            "ubicacion": "Oficina de Investigación - Edificio 67",
            "descripcion": "Medidor secundario para monitoreo de consumo en oficina de investigación.",
            "firmware_version": "1.0.0",
            "intervalo_medicion": 60,
        },
        {
            "device_id": "ESP32-003",
            "nombre": "Medidor Pruebas - Banco de trabajo",
            "ubicacion": "Banco de pruebas - Laboratorio",
            "descripcion": "Medidor de pruebas para validación de firmware y calibración.",
            "firmware_version": "1.0.0",
            "intervalo_medicion": 30,
        },
    ]

    creados = []
    existentes = []

    for data in dispositivos_seed:
        existente = db.query(Dispositivo).filter(
            Dispositivo.device_id == data["device_id"]
        ).first()

        if existente:
            existentes.append(data["device_id"])
            continue

        nuevo = Dispositivo(
            device_id=data["device_id"],
            nombre=data["nombre"],
            ubicacion=data["ubicacion"],
            descripcion=data["descripcion"],
            firmware_version=data["firmware_version"],
            intervalo_medicion=data["intervalo_medicion"],
            activo=True,
            conectado=False,
        )
        db.add(nuevo)
        creados.append(data["device_id"])

    if creados:
        db.commit()

    return {
        "mensaje": f"Seed completado: {len(creados)} creados, {len(existentes)} ya existían",
        "creados": creados,
        "existentes": existentes,
    }


@router.post("/seed-mediciones-demo")
def seed_mediciones_demo(db: Session = Depends(get_db)):
    """
    Genera mediciones demo para los dispositivos existentes.
    Útil para verificar que gráficas y tablas funcionen correctamente.
    Crea 24 mediciones (una por hora) para cada dispositivo.
    """
    import random
    from datetime import timedelta

    dispositivos = db.query(Dispositivo).filter(Dispositivo.activo == True).all()
    if not dispositivos:
        return {"mensaje": "No hay dispositivos. Ejecuta /admin/seed primero.", "mediciones_creadas": 0}

    total_creadas = 0
    ahora = datetime.now(timezone.utc)

    for disp in dispositivos:
        # Verificar si ya tiene mediciones recientes (últimas 24h)
        mediciones_existentes = db.query(Medicion).filter(
            Medicion.device_id == disp.device_id,
            Medicion.timestamp >= ahora - timedelta(hours=24),
        ).count()

        if mediciones_existentes >= 20:
            continue  # Ya tiene suficientes datos

        # Base values para cada dispositivo (ligeramente diferentes)
        base_voltage = 118.0 + random.uniform(-3, 5)
        base_current = 0.12 + random.uniform(0, 0.08)

        for i in range(24):
            hora = ahora - timedelta(hours=23 - i)

            # Simular variación horaria (más consumo en horas de trabajo)
            hour_of_day = hora.hour
            if 8 <= hour_of_day <= 18:
                load_factor = 1.0 + random.uniform(0, 0.5)
            elif 6 <= hour_of_day <= 22:
                load_factor = 0.6 + random.uniform(0, 0.3)
            else:
                load_factor = 0.2 + random.uniform(0, 0.2)

            voltage = base_voltage + random.uniform(-2, 2)
            current = base_current * load_factor + random.uniform(-0.02, 0.02)
            current = max(0.01, current)
            power = voltage * current
            pf = 0.92 + random.uniform(0, 0.08)
            freq = 59.9 + random.uniform(0, 0.2)
            energy = power * (1 / 60)  # Wh en 1 minuto

            medicion = Medicion(
                dispositivo_id=disp.id,
                device_id=disp.device_id,
                voltaje_rms=round(voltage, 2),
                corriente_rms=round(current, 4),
                potencia_activa=round(power, 2),
                potencia_reactiva=round(power * 0.1, 2),
                potencia_aparente=round(power / pf, 2),
                factor_potencia=round(pf, 3),
                frecuencia=round(freq, 2),
                energia_activa=round(energy, 4),
                energia_reactiva=round(energy * 0.05, 4),
                timestamp=hora,
            )
            db.add(medicion)
            total_creadas += 1

        # Actualizar estado del dispositivo
        disp.conectado = True
        disp.ultima_conexion = ahora

    db.commit()

    return {
        "mensaje": f"Demo completado: {total_creadas} mediciones creadas para {len(dispositivos)} dispositivos",
        "mediciones_creadas": total_creadas,
        "dispositivos": [d.device_id for d in dispositivos],
    }
