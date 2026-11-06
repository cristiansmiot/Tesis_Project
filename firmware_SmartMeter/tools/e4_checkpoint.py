"""Punto de control remoto de E4/P05 (solo lectura de Railway, sin dependencias).

No toca el medidor ni el servidor: descarga mediciones, salud (/estado) y
eventos desde el inicio de la corrida, guarda la copia cruda con su SHA-256 y
calcula los indicadores del protocolo de ensayos (seccion 4):

  - arranques (boot_id) y reinicios desde el inicio;
  - secuencias recibidas, huecos, duplicados;
  - entrega en vivo = recibidas / generadas (data_generated_count del ultimo
    /estado); la entrega final se calcula al cierre contra la auditoria SD;
  - latencia = llegada al servidor - hora del dispositivo (time_valid);
    p50, p95 y maximo;
  - cola pendiente estimada = generadas - publicadas OK (vista del medidor);
  - radio (RSSI, RSRP, RSRQ, SINR), reconexiones y alarmas activas.

Uso (en cualquier PC con Python 3.8+):
  python e4_checkpoint.py --inicio 2026-11-05T01:30:00Z --codigo E4-01
  python e4_checkpoint.py --inicio ... --codigo E4-01 --etiqueta "+6 h"

Salida: <evidence>/E4/<codigo>/checkpoints/<AAAAMMDDTHHMMSSZ>/ con
mediciones.json, salud.json, eventos.json, resumen.json y SHA256SUMS.txt, y
una fila nueva en <evidence>/E4/<codigo>/checkpoints.csv.
"""

import argparse
import csv
import hashlib
import json
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen

BASE = "https://tesisproject-production.up.railway.app/api/v1"
DEVICE = "SM-348518435344"
AQUI = Path(__file__).resolve().parent
EVIDENCIA = AQUI.parent / "evidence" / "E4"


def get_json(url, timeout=60, intentos=3):
    ultimo = None
    for n in range(intentos):
        try:
            req = Request(url, headers={"User-Agent": "e4-checkpoint/1.0"})
            with urlopen(req, timeout=timeout) as r:
                return json.load(r)
        except Exception as exc:  # noqa: BLE001
            ultimo = exc
            time.sleep(3 * (n + 1))
    raise RuntimeError(f"no se pudo leer {url}: {ultimo!r}")


def parse_ts(texto):
    if not texto:
        return None
    t = datetime.fromisoformat(texto.replace("Z", "+00:00"))
    return t if t.tzinfo else t.replace(tzinfo=timezone.utc)


def rangos(numeros):
    """[1,2,3,7,8] -> '1-3, 7-8'."""
    out, ini, prev = [], None, None
    for n in sorted(numeros):
        if ini is None:
            ini = prev = n
        elif n == prev + 1:
            prev = n
        else:
            out.append(f"{ini}-{prev}" if ini != prev else f"{ini}")
            ini = prev = n
    if ini is not None:
        out.append(f"{ini}-{prev}" if ini != prev else f"{ini}")
    return ", ".join(out)


def pct(valores, p):
    if not valores:
        return None
    v = sorted(valores)
    k = (len(v) - 1) * p / 100.0
    i, f = int(k), k - int(k)
    return v[i] if i + 1 >= len(v) else v[i] + (v[i + 1] - v[i]) * f


def descargar_mediciones(base, device, inicio):
    filas, skip = [], 0
    while True:
        q = urlencode({"device_id": device, "skip": skip, "limit": 250})
        page = get_json(f"{base}/mediciones/?{q}")
        if not page:
            break
        filas.extend(page)
        # Orden descendente por timestamp: parar al pasar el inicio.
        mas_antigua = parse_ts(page[-1].get("timestamp"))
        if mas_antigua is not None and mas_antigua < inicio:
            break
        skip += len(page)
        if skip > 20000:
            break
    return [f for f in filas if (parse_ts(f.get("timestamp")) or inicio) >= inicio]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inicio", required=True, help="Inicio de la corrida en UTC (ISO 8601)")
    ap.add_argument("--codigo", default="E4-01")
    ap.add_argument("--etiqueta", default="", help="Texto libre: '+6 h', 'final', ...")
    ap.add_argument("--device", default=DEVICE)
    ap.add_argument("--base-url", default=BASE)
    ap.add_argument("--salida", type=Path, default=EVIDENCIA)
    args = ap.parse_args(argv)

    inicio = parse_ts(args.inicio)
    ahora = datetime.now(timezone.utc)
    horas = max(1, min(168, int((ahora - inicio).total_seconds() // 3600) + 2))
    base = args.base_url.rstrip("/")

    med = descargar_mediciones(base, args.device, inicio)
    salud = get_json(f"{base}/salud/{args.device}/historico?horas={horas}&limit=500")
    # /historico llega en orden descendente: ordenar para que [-1] sea el ultimo.
    salud_rows = sorted((r for r in (salud.get("registros") or [])
                         if (parse_ts(r.get("timestamp")) or inicio) >= inicio),
                        key=lambda r: parse_ts(r.get("timestamp")))
    q = urlencode({"device_id": args.device, "desde": inicio.isoformat(), "limit": 500})
    ev = get_json(f"{base}/eventos/?{q}")
    ev_rows = ev.get("eventos", ev.get("items", [])) if isinstance(ev, dict) else ev
    q = urlencode({"device_id": args.device, "activo": "true", "limit": 200})
    act = get_json(f"{base}/eventos/?{q}")
    act_rows = act.get("eventos", act.get("items", [])) if isinstance(act, dict) else act

    # --- mediciones por arranque -------------------------------------------
    boots = {}
    for f in med:
        boots.setdefault(f.get("boot_id"), []).append(f)
    resumen_boots, lat = [], []
    recibidas_total, dup_total = 0, 0
    for bid, filas in sorted(boots.items(), key=lambda kv: min(x["seq"] for x in kv[1])
                             if kv[0] is not None else 0):
        seqs = sorted({x["seq"] for x in filas if x.get("seq") is not None})
        if not seqs:
            continue
        faltan = sorted(set(range(seqs[0], seqs[-1] + 1)) - set(seqs))
        dup = sum(int(x.get("duplicate_count") or 0) for x in filas)
        recibidas_total += len(seqs)
        dup_total += dup
        for x in filas:
            if x.get("time_valid") is False:
                continue
            t_dev, t_rx = parse_ts(x.get("timestamp")), parse_ts(x.get("created_at"))
            if t_dev and t_rx:
                lat.append((t_rx - t_dev).total_seconds())
        resumen_boots.append({"boot_id": bid, "seq_min": seqs[0], "seq_max": seqs[-1],
                              "recibidas": len(seqs), "huecos": rangos(faltan),
                              "n_huecos": len(faltan), "duplicados": dup,
                              "fw": sorted({x.get("fw_version") for x in filas})})

    ultimo = salud_rows[-1] if salud_rows else {}
    generadas = ultimo.get("data_generated_count")
    publicadas = ultimo.get("data_publish_ok")
    boot_actual = ultimo.get("boot_id")
    # Solo secuencias que ya existian cuando se emitio ese /estado (seq <= generadas).
    entrega_vivo = entrega_oportuna = None
    if generadas:
        filas_b = [x for x in boots.get(boot_actual, []) if (x.get("seq") or 0) <= generadas]
        seqs_b = {x["seq"] for x in filas_b}
        a_tiempo = set()
        for x in filas_b:
            t_dev, t_rx = parse_ts(x.get("timestamp")), parse_ts(x.get("created_at"))
            if t_dev and t_rx and (t_rx - t_dev).total_seconds() <= 120:
                a_tiempo.add(x["seq"])
        # La ventana puede empezar a mitad de un arranque (p. ej. tras
        # sincronizar la hora): el denominador cuenta desde su primera seq.
        seq_ini = min(seqs_b) if seqs_b else 1
        esperadas = max(1, generadas - seq_ini + 1)
        entrega_vivo = 100.0 * len(seqs_b) / esperadas
        entrega_oportuna = 100.0 * len(a_tiempo) / esperadas
    sinr = [r["sinr_db"] for r in salud_rows if r.get("sinr_db") is not None]
    rsrp = [r["rsrp_dbm"] for r in salud_rows if r.get("rsrp_dbm") is not None]

    resumen = {
        "codigo": args.codigo, "etiqueta": args.etiqueta,
        "checkpoint_utc": ahora.isoformat(timespec="seconds"),
        "inicio_utc": inicio.isoformat(timespec="seconds"),
        "horas_transcurridas": round((ahora - inicio).total_seconds() / 3600, 2),
        "arranques": [b["boot_id"] for b in resumen_boots],
        "reinicios_desde_inicio": max(0, len(resumen_boots) - 1),
        "por_arranque": resumen_boots,
        "recibidas_unicas": recibidas_total, "duplicados": dup_total,
        "ultimo_estado": {k: ultimo.get(k) for k in (
            "timestamp", "boot_id", "uptime_ms", "reset_reason_name", "fw_version",
            "ac_ok", "carga", "ade_ok", "modem_ok", "mqtt_ok", "rssi_dbm", "rsrp_dbm",
            "rsrq_db", "sinr_db", "data_generated_count", "data_publish_attempts",
            "data_publish_ok", "reconexiones", "red_intentos", "red_exitos",
            "mqtt_intentos", "mqtt_exitos", "ac_loss_count", "ac_restore_count")},
        "entrega_en_vivo_pct_boot_actual": None if entrega_vivo is None else round(entrega_vivo, 2),
        "entrega_oportuna_120s_pct_boot_actual": None if entrega_oportuna is None else round(entrega_oportuna, 2),
        "cola_estimada": (generadas - publicadas) if (generadas is not None and publicadas is not None) else None,
        "latencia_s": {"n": len(lat), "p50": pct(lat, 50), "p95": pct(lat, 95),
                       "max": max(lat) if lat else None,
                       "mayor_120s": sum(1 for v in lat if v > 120)},
        "radio": {"n_estados": len(salud_rows),
                  "sinr_db_mediana": statistics.median(sinr) if sinr else None,
                  "sinr_db_min": min(sinr) if sinr else None,
                  "rsrp_dbm_mediana": statistics.median(rsrp) if rsrp else None},
        "eventos_desde_inicio": len(ev_rows),
        "alarmas_activas": [{"id": e.get("id"), "tipo": e.get("tipo"), "code": e.get("code"),
                             "timestamp": e.get("timestamp")} for e in act_rows],
        "nota": "La entrega final y la cola real se calculan al cierre contra telemetry_audit_v1.jsonl de la SD.",
    }

    sello = ahora.strftime("%Y%m%dT%H%M%SZ")
    carpeta = args.salida / args.codigo / "checkpoints" / sello
    carpeta.mkdir(parents=True, exist_ok=True)
    archivos = {"mediciones.json": med, "salud.json": salud_rows,
                "eventos.json": ev_rows, "alarmas_activas.json": act_rows,
                "resumen.json": resumen}
    sumas = []
    for nombre, datos in archivos.items():
        p = carpeta / nombre
        p.write_text(json.dumps(datos, ensure_ascii=False, indent=1, default=str), encoding="utf-8")
        sumas.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {nombre}")
    (carpeta / "SHA256SUMS.txt").write_text("\n".join(sumas) + "\n", encoding="utf-8")

    tabla = args.salida / args.codigo / "checkpoints.csv"
    nueva = not tabla.exists()
    with tabla.open("a", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        if nueva:
            w.writerow(["checkpoint_utc", "etiqueta", "horas", "boot_id", "seq_max", "recibidas",
                        "generadas", "entrega_vivo_pct", "entrega_120s_pct", "cola_estimada", "huecos", "duplicados",
                        "reinicios", "lat_p50_s", "lat_p95_s", "lat_max_s", "rssi", "rsrp",
                        "rsrq", "sinr", "reconexiones", "alarmas_activas"])
        b = next((x for x in resumen_boots if x["boot_id"] == boot_actual), {})
        L = resumen["latencia_s"]
        w.writerow([resumen["checkpoint_utc"], args.etiqueta, resumen["horas_transcurridas"],
                    boot_actual, b.get("seq_max"), recibidas_total, generadas,
                    resumen["entrega_en_vivo_pct_boot_actual"],
                    resumen["entrega_oportuna_120s_pct_boot_actual"], resumen["cola_estimada"],
                    sum(x["n_huecos"] for x in resumen_boots), dup_total,
                    resumen["reinicios_desde_inicio"],
                    None if L["p50"] is None else round(L["p50"], 1),
                    None if L["p95"] is None else round(L["p95"], 1),
                    None if L["max"] is None else round(L["max"], 1),
                    ultimo.get("rssi_dbm"), ultimo.get("rsrp_dbm"), ultimo.get("rsrq_db"),
                    ultimo.get("sinr_db"), ultimo.get("reconexiones"), len(act_rows)])

    print(f"[{args.codigo} {args.etiqueta}] {resumen['horas_transcurridas']} h desde el inicio")
    print(f"  arranques: {resumen['arranques']}  (reinicios: {resumen['reinicios_desde_inicio']})")
    for b in resumen_boots:
        print(f"  boot {b['boot_id']}: seq {b['seq_min']}-{b['seq_max']}, recibidas {b['recibidas']}, "
              f"huecos {b['n_huecos']} [{b['huecos']}], duplicados {b['duplicados']}")
    print(f"  generadas (medidor) {generadas}, publicadas OK {publicadas}, cola estimada {resumen['cola_estimada']}")
    print(f"  entrega (boot actual, seq <= generadas): {resumen['entrega_en_vivo_pct_boot_actual']} %"
          f"  | oportuna <= 120 s: {resumen['entrega_oportuna_120s_pct_boot_actual']} %")
    L = resumen["latencia_s"]
    if L["n"]:
        print(f"  latencia: p50 {L['p50']:.1f} s, p95 {L['p95']:.1f} s, max {L['max']:.1f} s, >120 s: {L['mayor_120s']}")
    u = resumen["ultimo_estado"]
    print(f"  ultimo /estado {u.get('timestamp')}: AC {u.get('ac_ok')} ADE {u.get('ade_ok')} "
          f"MQTT {u.get('mqtt_ok')} RSSI {u.get('rssi_dbm')} RSRP {u.get('rsrp_dbm')} "
          f"SINR {u.get('sinr_db')} reconexiones {u.get('reconexiones')}")
    print(f"  alarmas activas: {len(act_rows)}  |  guardado en {carpeta}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
