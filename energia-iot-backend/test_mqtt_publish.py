"""
Script para probar el flujo completo: publicar un mensaje MQTT de prueba
simulando el ESP32/SIM7080G y verificar que el backend lo reciba y guarde.

Uso:
    1. Primero iniciar el backend: uvicorn app.main:app --reload
    2. En otra terminal: python test_mqtt_publish.py

Requisitos:
    pip install paho-mqtt
"""
import json
import time
import random
import sys

import paho.mqtt.client as mqtt

# === Configuracion del broker MQTT en Railway ===
BROKER_HOST = "shinkansen.proxy.rlwy.net"
BROKER_PORT = 58954
USERNAME = "medidor_iot"
PASSWORD = "Colombia2026$"

# === Configuracion del dispositivo simulado ===
DEVICE_ID = "ESP32-TEST-001"
TOPIC = f"medidor/{DEVICE_ID}/datos"


def generate_measurement():
    """Genera una medicion simulada realista para 120V/60Hz Colombia."""
    voltage = round(random.uniform(118.0, 122.0), 1)
    current = round(random.uniform(0.5, 5.0), 2)
    power = round(voltage * current * random.uniform(0.85, 0.99), 1)
    pf = round(random.uniform(0.85, 0.99), 2)
    frequency = round(random.uniform(59.8, 60.2), 1)
    energy = round(random.uniform(10.0, 500.0), 3)
    apparent_power = round(voltage * current, 1)
    reactive_power = round((apparent_power**2 - power**2)**0.5, 1)

    return {
        "voltage": voltage,
        "current": current,
        "power": power,
        "pf": pf,
        "frequency": frequency,
        "energy": energy,
        "reactive_power": reactive_power,
        "apparent_power": apparent_power,
    }


def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"Conectado al broker MQTT: {BROKER_HOST}:{BROKER_PORT}")
    else:
        print(f"Error de conexion: {reason_code}")
        sys.exit(1)


def on_publish(client, userdata, mid, reason_code, properties):
    print(f"  Mensaje publicado (mid={mid})")


def main():
    print("=" * 60)
    print("PRUEBA MQTT - Simulador de medidor ESP32/SIM7080G")
    print("=" * 60)
    print(f"Broker: {BROKER_HOST}:{BROKER_PORT}")
    print(f"Device: {DEVICE_ID}")
    print(f"Topic:  {TOPIC}")
    print()

    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="test_publisher_001",
        protocol=mqtt.MQTTv311,
    )
    client.username_pw_set(USERNAME, PASSWORD)
    client.on_connect = on_connect
    client.on_publish = on_publish

    try:
        client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
        client.loop_start()
        time.sleep(2)  # Esperar conexion

        if not client.is_connected():
            print("No se pudo conectar al broker. Verifica credenciales y host.")
            sys.exit(1)

        # Enviar 5 mediciones de prueba
        num_messages = 5
        print(f"Enviando {num_messages} mediciones de prueba...\n")

        for i in range(num_messages):
            data = generate_measurement()
            payload = json.dumps(data)

            print(f"[{i+1}/{num_messages}] Publicando en {TOPIC}:")
            print(f"  V={data['voltage']}V | I={data['current']}A | "
                  f"P={data['power']}W | PF={data['pf']} | F={data['frequency']}Hz")

            client.publish(TOPIC, payload, qos=0)
            time.sleep(3)  # Esperar entre mediciones

        print()
        print("=" * 60)
        print("Prueba completada!")
        print()
        print("Verifica en el backend:")
        print("  - Logs del backend (deben mostrar 'Medicion guardada')")
        print("  - GET http://localhost:8000/api/v1/mediciones/")
        print(f"  - GET http://localhost:8000/api/v1/mediciones/{DEVICE_ID}/ultima")
        print("  - GET http://localhost:8000/api/v1/mqtt/status")
        print("=" * 60)

    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
