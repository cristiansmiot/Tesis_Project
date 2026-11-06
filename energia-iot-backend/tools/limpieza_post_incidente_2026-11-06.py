"""Limpieza de PostgreSQL tras el incidente SQLite (2026-10-19 a 2026-11-06).

1. Cierra, sin borrarlas, las alarmas activas anteriores al 2026-10-19 01:00Z
   (POST /admin/cerrar-alarmas-antiguas: primero simula y pide confirmación).
2. Desactiva los medidores de demostración ESP32-001, ESP32-002 y ESP32-003
   (PATCH /dispositivos/{id} con activo=false; sus datos se conservan).

Pide usuario y contraseña de super_admin en la terminal y no los guarda.
Deja un registro JSON en firmware_SmartMeter/evidence/ (sin el token).

Uso:  python energia-iot-backend/tools/limpieza_post_incidente_2026-11-06.py
"""
import getpass
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

BASE = "https://tesisproject-production.up.railway.app"
API = f"{BASE}/api/v1"
CORTE = "2026-10-19T01:00:00Z"
DEMOS = ("ESP32-001", "ESP32-002", "ESP32-003")
REGISTRO = (Path(__file__).resolve().parents[2] / "firmware_SmartMeter" / "evidence"
            / "INCIDENTE_BD_limpieza_2026-11-06.json")


def llamar(metodo, url, token=None, cuerpo=None):
    datos = json.dumps(cuerpo).encode() if cuerpo is not None else None
    req = Request(url, data=datos, method=metodo)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urlopen(req, timeout=60) as r:
            return json.loads(r.read().decode() or "null")
    except HTTPError as e:
        sys.exit(f"{metodo} {url} -> HTTP {e.code}: {e.read().decode()[:300]}")


def main():
    registro = {"inicio_utc": datetime.now(timezone.utc).isoformat(), "corte": CORTE}

    salud = llamar("GET", f"{BASE}/health")
    print("health:", salud.get("database"))
    if salud.get("database", {}).get("type") != "postgresql":
        sys.exit("El backend no esta en PostgreSQL: no se hace nada.")
    registro["health"] = salud.get("database")

    email = input("Correo (super_admin): ").strip()
    clave = getpass.getpass("Contrasena: ")
    login = llamar("POST", f"{API}/auth/login", cuerpo={"email": email, "password": clave})
    token = login["access_token"]
    registro["usuario"] = email

    url = f"{API}/admin/cerrar-alarmas-antiguas?" + urlencode({"hasta": CORTE})
    simulacion = llamar("POST", url, token)
    registro["simulacion"] = simulacion
    print(f"\nAlarmas activas anteriores a {CORTE}: {simulacion['total']}")
    print(f"  de {simulacion['primera']} a {simulacion['ultima']}")
    for clave_tipo, n in sorted(simulacion["por_tipo"].items(), key=lambda x: -x[1]):
        print(f"  {n:4d}  {clave_tipo}")

    if simulacion["total"] and input("\nCerrarlas (sin borrarlas)? escriba SI: ").strip() == "SI":
        cierre = llamar("POST", url + "&confirmar=true", token)
        registro["cierre"] = cierre
        print(f"Cerradas: {cierre['cerradas']}")
    else:
        print("No se cerraron alarmas.")

    registro["dispositivos"] = {}
    for device_id in DEMOS:
        actual = llamar("GET", f"{API}/dispositivos/{device_id}", token)
        if actual.get("activo"):
            nuevo = llamar("PATCH", f"{API}/dispositivos/{device_id}", token, {"activo": False})
            registro["dispositivos"][device_id] = {"antes": True, "despues": nuevo.get("activo")}
            print(f"{device_id}: desactivado")
        else:
            registro["dispositivos"][device_id] = {"antes": False, "despues": False}
            print(f"{device_id}: ya estaba inactivo")

    registro["resumen_final"] = llamar("GET", f"{API}/dashboard/resumen")
    registro["fin_utc"] = datetime.now(timezone.utc).isoformat()
    REGISTRO.write_text(json.dumps(registro, ensure_ascii=False, indent=1), encoding="utf-8")
    print("\nResumen:", {k: registro["resumen_final"][k] for k in
                         ("total_dispositivos", "alarmas_activas")})
    print("Registro:", REGISTRO)


if __name__ == "__main__":
    main()
