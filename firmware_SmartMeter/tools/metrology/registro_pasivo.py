"""Registro pasivo de referencias del banco en paralelo a una corrida (p. ej. E4-02).

Solo CONSULTA los instrumentos. Nunca envia ordenes que cambien la salida del
PCR500MA ni la configuracion de los 34450A: no hay *RST, CONF, OUTP ni VOLT.
Cada orden pasa por SoloConsulta, que rechaza todo lo que no termine en '?'.
Asi se puede registrar durante una prueba de operacion continua sin
intervenirla. No usa FuentePCR.configurar()/cerrar(), que apagan la salida.

Unica excepcion opcional: con --perfil-frecuencia (CSV con columnas
inicio_utc,f_hz) se permite UNA orden de escritura, "FREQuency <f>", con f
entre 58 y 62 Hz. Despues de cada cambio se verifica FREQuency? y OUTPut?; si
la salida no esta encendida se suspenden las escrituras (nunca se enciende la
salida desde aqui). Al terminar se devuelve la consigna a 60 Hz.

Cada multimetro se usa solo si ya esta en la funcion esperada (ACV para el de
tension, ACI para el de corriente); si no, se deja fuera y se anota. El Fluke
43B (opcional) se lee con QM, que tampoco cambia su configuracion.

Mientras corre, pide a Windows que no suspenda el equipo.

Uso (desde firmware_SmartMeter):
  python tools/metrology/registro_pasivo.py --hasta 2026-11-07T17:40:00Z \
      --salida evidence/E4/E4-02/referencias [--fluke COM8] [--periodo 10]
  python tools/metrology/registro_pasivo.py --hasta ... --simular --periodo 1
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import random
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))

import instrumentos as ins  # noqa: E402

BOGOTA = timezone(timedelta(hours=-5))
CAMPOS = ["t_utc", "t_bogota", "epoch", "pcr_salida", "pcr_f_consigna", "pcr_v",
          "pcr_i", "pcr_p", "pcr_pf", "dmm_v", "dmm_i", "fluke_lecturas", "evento",
          "errores", "pcr_crudo"]
F_MIN_PERMITIDA, F_MAX_PERMITIDA, F_NOMINAL = 58.0, 62.0, 60.0


class SoloConsulta:
    """Envoltura de un recurso VISA que solo permite consultas SCPI."""

    def __init__(self, recurso, timeout_ms=10000):
        import pyvisa

        self.recurso = recurso
        self.inst = pyvisa.ResourceManager().open_resource(recurso)
        self.inst.timeout = timeout_ms
        self.inst.read_termination = "\n"
        self.inst.write_termination = "\n"

    def q(self, orden):
        orden = orden.strip()
        if not orden.endswith("?") or ";" in orden:
            raise PermissionError(f"orden no permitida en registro pasivo: {orden!r}")
        return self.inst.query(orden).strip()

    def fijar_frecuencia(self, f_hz):
        """Unica escritura permitida: consigna de frecuencia del PCR."""
        if not (F_MIN_PERMITIDA <= f_hz <= F_MAX_PERMITIDA):
            raise PermissionError(f"frecuencia fuera del rango permitido: {f_hz}")
        self.inst.write(f"FREQuency {f_hz:.3f}")

    def cerrar(self):
        try:
            self.inst.close()
        except Exception:  # noqa: BLE001
            pass


class Simulado:
    """Respuestas sinteticas para probar el registrador sin banco."""

    def __init__(self, recurso, funcion):
        self.recurso, self.funcion = recurso, funcion
        self.f = 60.0

    def fijar_frecuencia(self, f_hz):
        if not (F_MIN_PERMITIDA <= f_hz <= F_MAX_PERMITIDA):
            raise PermissionError(f"frecuencia fuera del rango permitido: {f_hz}")
        self.f = f_hz

    def q(self, orden):
        if orden == "*IDN?":
            return f"SIMULADO,{self.funcion},0,0"
        if orden == "OUTPut?":
            return "1"
        if orden == "VOLTage?":
            return "115.00"
        if orden == "FREQuency?":
            return f"{self.f:.3f}"
        if orden == "CONFigure?":
            return '"VOLT:AC +1.000000E+02"' if self.funcion == "DMM_V" else '"CURR:AC +1.000000E+00"'
        if orden == "MEASure:ALL?":
            v = 115.0 + random.gauss(0, 0.02)
            crudo = [0.0] * 18
            crudo[ins.PCR_IDX_V], crudo[ins.PCR_IDX_I] = v, 0.302
            crudo[ins.PCR_IDX_P], crudo[ins.PCR_IDX_PF] = 22.9, 0.66
            return ",".join(f"{x:.5g}" for x in crudo)
        if orden == "READ?":
            return f"{114.93 + random.gauss(0, 0.01):.5f}" if self.funcion == "DMM_V" else "0.30170"
        raise PermissionError(orden)

    def cerrar(self):
        pass


def mantener_despierto(activo):
    """ES_CONTINUOUS | ES_SYSTEM_REQUIRED mientras corre; se libera al salir."""
    if sys.platform != "win32":
        return
    import ctypes

    estado = 0x80000000 | (0x00000001 if activo else 0)
    ctypes.windll.kernel32.SetThreadExecutionState(estado)


def leer_numero(texto):
    valor = float(texto.split(",")[0])
    return math.nan if abs(valor) >= ins.DMM_OVERLOAD else valor


class Instrumento:
    """Apertura diferida y reapertura tras errores (p. ej. caida del USB)."""

    def __init__(self, nombre, recurso, simular, funcion_esperada=None):
        self.nombre, self.recurso, self.simular = nombre, recurso, simular
        self.funcion_esperada = funcion_esperada
        self.h = None
        self.excluido = None
        self.meta = {}

    def abrir(self):
        if self.h is None:
            self.h = Simulado(self.recurso, self.nombre) if self.simular else SoloConsulta(self.recurso)
            self.meta["idn"] = self.h.q("*IDN?")
            if self.funcion_esperada:
                conf = self.h.q("CONFigure?").strip('"')
                self.meta["configure"] = conf
                if not conf.upper().startswith(self.funcion_esperada):
                    self.excluido = f"{self.nombre} en '{conf}', se esperaba {self.funcion_esperada}"
        return self.h

    def q(self, orden):
        try:
            return self.abrir().q(orden)
        except PermissionError:
            raise
        except Exception:
            if self.h is not None:
                self.h.cerrar()
            self.h = None
            raise

    def cerrar(self):
        if self.h is not None:
            self.h.cerrar()  # solo cierra la sesion VISA: no envia ordenes
            self.h = None


def leer_perfil(ruta):
    """[(inicio_utc, f_hz)] ordenado; valida el rango de cada consigna."""
    pasos = []
    with open(ruta, encoding="utf-8") as f:
        for fila in csv.DictReader(linea for linea in f if not linea.startswith("#")):
            t = datetime.fromisoformat(fila["inicio_utc"].replace("Z", "+00:00"))
            fz = float(fila["f_hz"])
            if not (F_MIN_PERMITIDA <= fz <= F_MAX_PERMITIDA):
                raise SystemExit(f"perfil: {fz} Hz fuera de {F_MIN_PERMITIDA}-{F_MAX_PERMITIDA}")
            pasos.append((t, fz))
    return sorted(pasos)


def cambiar_frecuencia(pcr, f_hz):
    """Escribe la consigna y verifica; devuelve (texto_evento, seguir_escribiendo)."""
    pcr.abrir().fijar_frecuencia(f_hz)
    time.sleep(1.0)
    leida = float(pcr.q("FREQuency?"))
    salida = pcr.q("OUTPut?").strip()
    if salida not in ("1", "ON"):
        return f"FREQ->{f_hz:.3f} leida={leida:.3f} SALIDA={salida}: escrituras suspendidas", False
    ok = abs(leida - f_hz) < 0.001
    return f"FREQ->{f_hz:.3f} leida={leida:.3f} {'OK' if ok else 'NO COINCIDE'}", True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--hasta", required=True, help="Fin del registro en UTC (ISO 8601)")
    ap.add_argument("--periodo", type=float, default=10.0, help="Segundos entre lecturas")
    ap.add_argument("--salida", type=Path, required=True)
    ap.add_argument("--pcr", default=ins.PCR_VISA)
    ap.add_argument("--dmm-v", default=ins.DMM_VISA)
    ap.add_argument("--dmm-i", default=ins.DMM_I_VISA)
    ap.add_argument("--sin-dmm-i", action="store_true")
    ap.add_argument("--fluke", default="", help="Puerto del Fluke 43B (opcional)")
    ap.add_argument("--perfil-frecuencia", type=Path, default=None,
                    help="CSV inicio_utc,f_hz: unica escritura permitida al PCR")
    ap.add_argument("--simular", action="store_true")
    args = ap.parse_args(argv)
    perfil = leer_perfil(args.perfil_frecuencia) if args.perfil_frecuencia else []

    hasta = datetime.fromisoformat(args.hasta.replace("Z", "+00:00"))
    if hasta.tzinfo is None:
        hasta = hasta.replace(tzinfo=timezone.utc)
    sello = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    args.salida.mkdir(parents=True, exist_ok=True)
    ruta_csv = args.salida / f"registro_pasivo_{sello}.csv"
    ruta_fluke = args.salida / f"fluke_{sello}.csv"
    ruta_meta = args.salida / f"registro_pasivo_{sello}.json"
    ruta_log = args.salida / f"registro_pasivo_{sello}.log"

    def log(texto):
        print(texto, flush=True)
        with ruta_log.open("a", encoding="utf-8") as fl:
            fl.write(texto + "\n")

    pcr = Instrumento("PCR", args.pcr, args.simular)
    dmm_v = Instrumento("DMM_V", args.dmm_v, args.simular, "VOLT:AC")
    dmm_i = None if args.sin_dmm_i else Instrumento("DMM_I", args.dmm_i, args.simular, "CURR:AC")
    fluke = None
    if args.fluke and not args.simular:
        fluke = ins.Fluke43B(args.fluke)

    meta = {"inicio_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "hasta_utc": hasta.isoformat(), "periodo_s": args.periodo,
            "simulado": args.simular, "csv": ruta_csv.name, "nota": __doc__.split("\n\n")[1],
            "perfil_frecuencia": [(t.isoformat(), fz) for t, fz in perfil]}
    for inst in (pcr, dmm_v, dmm_i):
        if inst is None:
            continue
        try:
            inst.abrir()
            if inst is pcr:
                for orden in ("OUTPut?", "VOLTage?", "FREQuency?"):
                    inst.meta[orden] = inst.q(orden)
        except Exception as exc:  # noqa: BLE001
            inst.meta["error_apertura"] = repr(exc)
        if inst.excluido:
            inst.meta["excluido"] = inst.excluido
        meta[inst.nombre] = inst.meta
        log(f"{inst.nombre} {inst.meta}")
    if fluke is not None:
        try:
            meta["FLUKE"] = {"idn": fluke.abrir(), "puerto": args.fluke}
        except Exception as exc:  # noqa: BLE001
            meta["FLUKE"] = {"error_apertura": repr(exc)}
            fluke = None
        log(f"FLUKE {meta['FLUKE']}")
    ruta_meta.write_text(json.dumps(meta, ensure_ascii=False, indent=1), encoding="utf-8")

    mantener_despierto(True)
    filas = errores_total = 0
    escribir = bool(perfil)
    f_aplicada = None
    try:
        with ruta_csv.open("w", newline="", encoding="utf-8") as f, \
                (ruta_fluke.open("w", newline="", encoding="utf-8") if fluke else open("nul" if sys.platform == "win32" else "/dev/null", "w")) as ff:
            w = csv.writer(f)
            w.writerow(CAMPOS)
            wf = csv.writer(ff)
            if fluke:
                wf.writerow(ins.FLUKE_CSV_HEADER)
            proximo = time.monotonic()
            while datetime.now(timezone.utc) < hasta:
                ahora = datetime.now(timezone.utc)
                fila = {"t_utc": ahora.isoformat(timespec="milliseconds"),
                        "t_bogota": ahora.astimezone(BOGOTA).isoformat(timespec="seconds"),
                        "epoch": f"{ahora.timestamp():.3f}"}
                errores = []
                vigentes = [fz for t, fz in perfil if t <= ahora]
                if escribir and vigentes and vigentes[-1] != f_aplicada:
                    try:
                        evento, escribir = cambiar_frecuencia(pcr, vigentes[-1])
                        f_aplicada = vigentes[-1]
                        fila["evento"] = evento
                        log(f"{fila['t_bogota']} {evento}")
                    except Exception as exc:  # noqa: BLE001
                        errores.append(f"PCR FREQ {type(exc).__name__}: {str(exc)[:80]}")
                try:
                    fila["pcr_salida"] = pcr.q("OUTPut?")
                    fila["pcr_f_consigna"] = float(pcr.q("FREQuency?"))
                    crudo = [float(x) for x in pcr.q("MEASure:ALL?").split(",")]
                    if len(crudo) == 18:
                        fila.update(pcr_v=crudo[ins.PCR_IDX_V], pcr_i=crudo[ins.PCR_IDX_I],
                                    pcr_p=crudo[ins.PCR_IDX_P], pcr_pf=crudo[ins.PCR_IDX_PF],
                                    pcr_crudo=";".join(f"{x:.6g}" for x in crudo))
                    else:
                        errores.append(f"PCR MEAS:ALL? {len(crudo)} valores")
                except Exception as exc:  # noqa: BLE001
                    errores.append(f"PCR {type(exc).__name__}: {str(exc)[:80]}")
                for inst, campo in ((dmm_v, "dmm_v"), (dmm_i, "dmm_i")):
                    if inst is None or inst.excluido:
                        continue
                    try:
                        fila[campo] = leer_numero(inst.q("READ?"))
                    except Exception as exc:  # noqa: BLE001
                        errores.append(f"{inst.nombre} {type(exc).__name__}: {str(exc)[:80]}")
                if fluke is not None:
                    try:
                        lecturas = fluke.leer()
                        t_loc = ahora.astimezone(BOGOTA).isoformat(timespec="seconds")
                        for d, crudo_f in lecturas:
                            wf.writerow(fluke.fila_csv(t_loc, fila["t_utc"], d, crudo_f))
                        fila["fluke_lecturas"] = len(lecturas)
                        ff.flush()
                    except Exception as exc:  # noqa: BLE001
                        errores.append(f"FLUKE {type(exc).__name__}: {str(exc)[:80]}")
                fila["errores"] = " | ".join(errores)
                errores_total += bool(errores)
                w.writerow([fila.get(c, "") for c in CAMPOS])
                f.flush()
                filas += 1
                if filas % 60 == 1:
                    log(f"{fila['t_bogota']} filas={filas} f={fila.get('pcr_f_consigna', '')} "
                        f"V_pcr={fila.get('pcr_v', '')} V_dmm={fila.get('dmm_v', '')} "
                        f"I_dmm={fila.get('dmm_i', '')} P_pcr={fila.get('pcr_p', '')} "
                        f"errores={errores_total}")
                proximo += args.periodo
                time.sleep(max(0.0, proximo - time.monotonic()))
    except KeyboardInterrupt:
        log("Detenido por el usuario")
    finally:
        if perfil and escribir and f_aplicada not in (None, F_NOMINAL):
            try:
                evento, _ = cambiar_frecuencia(pcr, F_NOMINAL)
                log(f"cierre: {evento}")
            except Exception as exc:  # noqa: BLE001
                log(f"cierre: no se pudo volver a 60 Hz: {exc!r}")
        mantener_despierto(False)
        for inst in (pcr, dmm_v, dmm_i):
            if inst is not None:
                inst.cerrar()
        if fluke is not None:
            fluke.cerrar()  # solo cierra el puerto serie

    meta.update(fin_utc=datetime.now(timezone.utc).isoformat(timespec="seconds"),
                filas=filas, filas_con_error=errores_total,
                sha256_csv=hashlib.sha256(ruta_csv.read_bytes()).hexdigest())
    ruta_meta.write_text(json.dumps(meta, ensure_ascii=False, indent=1), encoding="utf-8")
    log(f"Fin: {filas} filas, {errores_total} con error -> {ruta_csv}")


if __name__ == "__main__":
    main()
