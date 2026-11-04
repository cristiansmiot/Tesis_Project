"""Ejecuta un escenario de escalones V/f en el PCR500MA y registra referencias.

Uso tipico (banco real):
    python ejecutar_escenario.py escenarios/P01_tension.csv --carga "LED OSRAM 15 W"

Ensayo en seco sin instrumentos:
    python ejecutar_escenario.py escenarios/00_humo.csv --simular --rapido 30

Salida (protocolo Obj. 4, seccion 10), una carpeta por corrida:
    <salida>/<prueba>_<AAAAMMDD>_runNN/
        referencia.csv      muestras PCR + 34450A con hora UTC y escalon
        pasos.csv           inicio/fin de estabilizacion y medicion por escalon
        fluke43b.csv        lecturas QM del Fluke (si --fluke)
        configuracion.json  instrumentos, escenario, carga, DUT y estado
        eventos.log         bitacora de acciones
        sha256.txt          huellas de los archivos crudos al cerrar

La salida del PCR se apaga siempre al terminar, ante error o con Ctrl+C.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import platform
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import instrumentos as ins

AQUI = Path(__file__).resolve().parent
SALIDA_DEFECTO = AQUI.parents[1] / "evidence" / "obj4_metrologia"
VERSION = "1.1"
# Registrador del Fluke 43B ya probado en el banco (tools/fluke43b_logger).
FLUKE_EXE = AQUI.parents[2] / "tools" / "fluke43b_logger" / "Fluke43BLogger.exe"

# Preambulo de sincronia: patron de tension inconfundible para alinear el
# reloj del DUT (uptime o RTC) con el del PC por correlacion.
PREAMBULO_SYNC = [
    ("SYNC_A", 120.0, 40), ("SYNC_B", 110.0, 20), ("SYNC_C", 130.0, 20),
    ("SYNC_D", 115.0, 20), ("SYNC_E", 120.0, 30),
]

REF_CAMPOS = ["t_utc", "t_epoch", "t_rel_s", "paso", "tipo", "etiqueta", "fase",
              "v_set", "f_set", "salida", "pcr_v", "pcr_i", "pcr_p", "pcr_pf",
              "dmm_modo", "dmm_rango", "dmm_valor", "dmm_f", "t_pcr", "t_dmm",
              "pcr_crudo"]
PASO_CAMPOS = ["paso", "tipo", "etiqueta", "v_set", "f_set", "estabilizacion_s",
               "medicion_s", "accion", "nota", "t_inicio_utc", "t_inicio",
               "t_medicion", "t_fin", "dmm_modo", "dmm_rango", "t_cambio"]


@dataclass
class Paso:
    paso: str
    tipo: str
    etiqueta: str
    v: float
    f: float
    estab_s: float
    med_s: float
    accion: str = ""
    nota: str = ""


@dataclass
class Escenario:
    ruta: Path
    meta: dict = field(default_factory=dict)
    pasos: list = field(default_factory=list)


def leer_escenario(ruta):
    ruta = Path(ruta)
    meta, filas = {}, []
    with ruta.open(encoding="utf-8-sig") as fh:
        for linea in fh:
            if linea.startswith("#"):
                clave, _, valor = linea[1:].partition(":")
                if valor:
                    meta[clave.strip().lower()] = valor.strip()
            elif linea.strip():
                filas.append(linea)
    pasos = []
    for r in csv.DictReader(filas):
        pasos.append(Paso(paso=r["paso"].strip(), tipo=r["tipo"].strip().upper(),
                          etiqueta=r["etiqueta"].strip(), v=float(r["v_ac"]),
                          f=float(r["f_hz"]), estab_s=float(r["estabilizacion_s"]),
                          med_s=float(r["medicion_s"]),
                          accion=(r.get("accion") or "").strip().lower(),
                          nota=(r.get("nota") or "").strip()))
    if not pasos:
        raise ValueError(f"{ruta}: escenario sin pasos")
    return Escenario(ruta=ruta, meta=meta, pasos=pasos)


def ahora_iso(t=None, local=False):
    t = time.time() if t is None else t
    if local:
        return datetime.fromtimestamp(t).astimezone().isoformat(timespec="milliseconds")
    return datetime.fromtimestamp(t, timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def sha256(ruta):
    h = hashlib.sha256()
    with open(ruta, "rb") as fh:
        for bloque in iter(lambda: fh.read(1 << 16), b""):
            h.update(bloque)
    return h.hexdigest()


class Estado:
    """Contexto del escalon vigente, compartido con los hilos de muestreo."""

    def __init__(self):
        self.lock = threading.Lock()
        self.d = {"paso": "", "tipo": "", "etiqueta": "", "fase": "inicio",
                  "v_set": 0.0, "f_set": 0.0, "salida": 0}

    def actualizar(self, **kw):
        with self.lock:
            self.d.update(kw)

    def copia(self):
        with self.lock:
            return dict(self.d)


class Corrida:
    def __init__(self, args, escenario, pasos, carpeta, pcr, dmm, fluke):
        self.args, self.esc, self.pasos = args, escenario, pasos
        self.carpeta, self.pcr, self.dmm, self.fluke = carpeta, pcr, dmm, fluke
        self.visa = threading.Lock()
        self.estado = Estado()
        self.parar = threading.Event()
        self.t0 = time.time()
        self.dmm_cfg = (None, None)
        self.log_fh = (carpeta / "eventos.log").open("a", encoding="utf-8")
        self.errores_muestreo = 0
        self.fluke_proc = None

    # -- Fluke 43B con su registrador .exe ---------------------------------
    def iniciar_logger_fluke(self):
        salida = self.carpeta / "fluke43b.csv"
        log = (self.carpeta / "fluke43b_logger.log").open("w", encoding="utf-8")
        cmd = [str(self.args.fluke_exe), "--port", self.args.fluke,
               "--interval", str(max(1, int(round(self.args.periodo_fluke)))),
               "--output", str(salida)]
        self.fluke_proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT,
                                           cwd=str(self.carpeta))
        self.log(f"Fluke43BLogger.exe iniciado en {self.args.fluke} (pid {self.fluke_proc.pid})")

    def detener_logger_fluke(self):
        if self.fluke_proc is None:
            return
        if self.fluke_proc.poll() is None:
            self.fluke_proc.terminate()
            try:
                self.fluke_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.fluke_proc.kill()
        self.log(f"Fluke43BLogger.exe detenido (codigo {self.fluke_proc.returncode})")

    # -- bitacora ---------------------------------------------------------
    def log(self, msg):
        linea = f"{ahora_iso()} {msg}"
        print(linea, flush=True)
        self.log_fh.write(linea + "\n")
        self.log_fh.flush()

    # -- espera interrumpible ---------------------------------------------
    def esperar(self, segundos):
        fin = time.monotonic() + segundos
        while not self.parar.is_set():
            resto = fin - time.monotonic()
            if resto <= 0:
                return
            time.sleep(min(resto, 0.25))

    # -- 34450A -----------------------------------------------------------
    def rango_dmm(self, v):
        if self.args.dmm_modo == "ACI":
            return float(self.args.dmm_rango or 1)
        if self.args.dmm_rango not in (None, "auto"):
            return float(self.args.dmm_rango)
        return 100.0 if v <= self.args.umbral_100v else 750.0

    def asegurar_dmm(self, v):
        cfg = (self.args.dmm_modo, self.rango_dmm(v))
        if cfg != self.dmm_cfg:
            with self.visa:
                errores = self.dmm.configurar(cfg[0], int(cfg[1]) if cfg[1] >= 1 else cfg[1])
            self.dmm_cfg = cfg
            self.log(f"DMM {cfg[0]} rango {cfg[1]:g} (frecuencia secundaria="
                     f"{'si' if self.dmm.secundaria else 'no'}) {errores or ''}")

    # -- muestreo PCR + DMM -------------------------------------------------
    def hilo_muestreo(self):
        ruta = self.carpeta / "referencia.csv"
        with ruta.open("w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=REF_CAMPOS)
            w.writeheader()
            proximo = time.monotonic()
            while not self.parar.is_set():
                ctx = self.estado.copia()
                try:
                    with self.visa:
                        ta = time.time()
                        m = self.pcr.medir()
                        tb = time.time()
                        dv, df = self.dmm.leer()
                        tc = time.time()
                    fila = dict(ctx)
                    fila.update(m)
                    fila.update({
                        "t_epoch": f"{(ta + tc) / 2:.3f}",
                        "t_utc": ahora_iso((ta + tc) / 2),
                        "t_rel_s": f"{(ta + tc) / 2 - self.t0:.3f}",
                        "dmm_modo": self.dmm_cfg[0], "dmm_rango": self.dmm_cfg[1],
                        "dmm_valor": "" if math.isnan(dv) else f"{dv:.6f}",
                        "dmm_f": "" if math.isnan(df) else f"{df:.6f}",
                        "t_pcr": f"{(ta + tb) / 2:.3f}", "t_dmm": f"{(tb + tc) / 2:.3f}",
                    })
                    w.writerow({k: fila.get(k, "") for k in REF_CAMPOS})
                    fh.flush()
                except Exception as exc:  # noqa: BLE001 - se registra y sigue
                    self.errores_muestreo += 1
                    self.log(f"ERROR muestreo: {exc!r}")
                    time.sleep(1.0)
                proximo += self.args.periodo
                espera = proximo - time.monotonic()
                if espera > 0:
                    time.sleep(espera)
                else:
                    proximo = time.monotonic()

    # -- Fluke 43B en paralelo -------------------------------------------
    def hilo_fluke(self):
        ruta = self.carpeta / "fluke43b.csv"
        nuevo = not ruta.exists()
        with ruta.open("a", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh, quoting=csv.QUOTE_ALL)
            if nuevo:
                w.writerow(ins.FLUKE_CSV_HEADER)
            conectado = False
            while not self.parar.is_set():
                try:
                    if not conectado:
                        self.fluke.abrir()
                        conectado = True
                        self.log(f"Fluke conectado {self.fluke.identidad} @ {self.fluke.baud}")
                    ta = time.time()
                    lecturas = self.fluke.leer()
                    t = (ta + time.time()) / 2
                    t_local = ahora_iso(t, local=True)
                    for d, crudo in lecturas:
                        w.writerow(self.fluke.fila_csv(t_local, ahora_iso(t), d, crudo))
                    fh.flush()
                except Exception as exc:  # noqa: BLE001
                    self.log(f"ERROR Fluke: {exc!r}")
                    self.fluke.cerrar()
                    conectado = False
                    self.esperar(2.0)
                self.esperar(self.args.periodo_fluke)
            self.fluke.cerrar()

    # -- control de la fuente ---------------------------------------------
    def encender_con_rampa(self, v, f):
        with self.visa:
            self.pcr.fijar(0.0, f)
            self.pcr.salida(True)
        n = max(1, int(self.args.rampa_s / 0.25))
        for k in range(1, n + 1):
            with self.visa:
                self.pcr.fijar_tension(v * k / n)
            time.sleep(self.args.rampa_s / n)
        self.estado.actualizar(salida=1)
        self.log(f"salida ON con rampa {self.args.rampa_s:g} s hasta {v:g} V {f:g} Hz")

    def apagar(self, motivo):
        with self.visa:
            self.pcr.salida(False)
        self.estado.actualizar(salida=0, fase="pausa")
        self.log(f"salida OFF ({motivo})")

    def pedir_confirmacion(self, texto):
        if self.args.sin_pausas:
            self.log(f"PAUSA omitida (--sin-pausas): {texto}")
            return
        self.log(f"PAUSA: {texto}")
        input(f"\n>>> {texto}\n    Salida del PCR APAGADA. ENTER para continuar (Ctrl+C aborta)... ")

    def ejecutar(self):
        pasos_csv = (self.carpeta / "pasos.csv").open("w", newline="", encoding="utf-8")
        wp = csv.DictWriter(pasos_csv, fieldnames=PASO_CAMPOS)
        wp.writeheader()
        hilos = [threading.Thread(target=self.hilo_muestreo, daemon=True)]
        if self.fluke is not None:
            hilos.append(threading.Thread(target=self.hilo_fluke, daemon=True))
        try:
            primero = self.pasos[0]
            self.asegurar_dmm(primero.v)
            if self.args.fluke and self.fluke is None:
                self.iniciar_logger_fluke()
            for h in hilos:
                h.start()
            encendida = False
            for k, p in enumerate(self.pasos):
                if self.parar.is_set():
                    break
                if p.accion == "cambio_carga" or not encendida:
                    if encendida:
                        self.apagar("cambio de carga")
                    if p.accion == "cambio_carga" or p.nota:
                        self.pedir_confirmacion(p.nota or "Confirmar montaje antes de energizar")
                    self.encender_con_rampa(p.v, p.f)
                    encendida = True
                else:
                    with self.visa:
                        self.pcr.fijar(p.v, p.f)
                # Instante en que el PCR ya aplico la consigna. t_inicio se
                # toma despues del cambio de rango del DMM, que puede esperar
                # el bloqueo VISA hasta ~9 s (READ? con frecuencia secundaria);
                # t_cambio sirve para medir latencias del DUT (p. ej. AC_LOST)
                # sin alterar las ventanas de medicion existentes.
                t_cambio = time.time()
                self.asegurar_dmm(p.v)
                t_ini = time.time()
                self.estado.actualizar(paso=p.paso, tipo=p.tipo, etiqueta=p.etiqueta,
                                       fase="estabilizacion", v_set=p.v, f_set=p.f)
                self.log(f"[{k + 1}/{len(self.pasos)}] {p.tipo} {p.etiqueta}: {p.v:g} V "
                         f"{p.f:g} Hz, estabiliza {p.estab_s:g} s, mide {p.med_s:g} s")
                self.esperar(p.estab_s)
                t_med = time.time()
                self.estado.actualizar(fase="medicion")
                self.esperar(p.med_s)
                t_fin = time.time()
                self.estado.actualizar(fase="transicion")
                wp.writerow({"paso": p.paso, "tipo": p.tipo, "etiqueta": p.etiqueta,
                             "v_set": p.v, "f_set": p.f, "estabilizacion_s": p.estab_s,
                             "medicion_s": p.med_s, "accion": p.accion, "nota": p.nota,
                             "t_inicio_utc": ahora_iso(t_ini), "t_inicio": f"{t_ini:.3f}",
                             "t_medicion": f"{t_med:.3f}", "t_fin": f"{t_fin:.3f}",
                             "dmm_modo": self.dmm_cfg[0], "dmm_rango": self.dmm_cfg[1],
                             "t_cambio": f"{t_cambio:.3f}"})
                pasos_csv.flush()
            return "completa" if not self.parar.is_set() else "interrumpida"
        except KeyboardInterrupt:
            self.log("Ctrl+C: abortando corrida")
            return "interrumpida"
        finally:
            try:
                self.apagar("fin de corrida")
            except Exception as exc:  # noqa: BLE001
                self.log(f"ERROR apagando salida: {exc!r} -- APAGAR EL PCR MANUALMENTE")
            self.parar.set()
            for h in hilos:
                if h.is_alive():
                    h.join(timeout=10)
            self.detener_logger_fluke()
            pasos_csv.close()


def carpeta_corrida(base, prueba):
    base.mkdir(parents=True, exist_ok=True)
    fecha = datetime.now().strftime("%Y%m%d")
    n = 1
    while (base / f"{prueba}_{fecha}_run{n:02d}").exists():
        n += 1
    destino = base / f"{prueba}_{fecha}_run{n:02d}"
    destino.mkdir()
    return destino


def construir_pasos(esc, args):
    pasos = []
    propios = [Paso(**asdict(p)) for p in esc.pasos]
    if not args.sin_sincronia:
        f0 = propios[0].f
        for i, (etq, v, dur) in enumerate(PREAMBULO_SYNC, 1):
            pasos.append(Paso(paso=f"S{i}", tipo="SYNC", etiqueta=etq, v=v, f=f0,
                              estab_s=0.0, med_s=float(dur)))
        # El montaje del primer escalon (p.ej. conectar la carga) se pide
        # antes del preambulo, para sincronizar ya con la carga del ensayo.
        pasos[0].accion, pasos[0].nota = propios[0].accion, propios[0].nota
        propios[0].accion, propios[0].nota = "", ""
    pasos += propios
    if args.rapido > 1:
        for p in pasos:
            p.estab_s /= args.rapido
            p.med_s /= args.rapido
    return pasos


def validar(pasos, args):
    v_max = max(p.v for p in pasos)
    f_min, f_max = min(p.f for p in pasos), max(p.f for p in pasos)
    if v_max > args.v_max:
        raise SystemExit(f"Escenario pide {v_max} V > limite --v-max {args.v_max} V")
    if f_min < 45 or f_max > 65:
        raise SystemExit("Frecuencias fuera de 45-65 Hz: el DUT no las detecta como red valida")
    return v_max, f_min, f_max


def verificar_banco(args, pcr, dmm, v_max, f_min, f_max):
    """Comprueba comunicacion y comandos sin encender la salida del PCR."""
    ok = True
    try:
        print("PCR:", pcr.abrir())
        print("DMM:", dmm.abrir())
        errores = pcr.configurar(min(150.0, v_max + 2.0), max(40.0, f_min - 2.0),
                                 min(70.0, f_max + 2.0), args.i_lim)
        print("PCR configuracion (salida OFF):", errores or "OK")
        ok = ok and not errores
        m = pcr.medir()
        print(f"PCR MEAS:ALL? V={m['pcr_v']:.3f} I={m['pcr_i']:.4f} "
              f"P={m['pcr_p']:.3f} PF={m['pcr_pf']:.3f}")
        if args.dmm_modo == "ACV":
            rangos = [("ACV", 750), ("ACV", 100)]
        else:
            rangos = [("ACI", float(args.dmm_rango or 1))]
        for modo, rango in rangos:
            errores = dmm.configurar(modo, rango)
            sec = "si" if dmm.secundaria else "NO (se usara la consigna del PCR)"
            print(f"DMM {modo} {rango:g}: frecuencia secundaria={sec} "
                  f"errores={errores or 'ninguno'}")
            for _ in range(3):
                t = time.time()
                v, f = dmm.leer()
                print(f"   READ? valor={v} f={f} ({time.time() - t:.2f} s)")
        if args.fluke and not args.fluke_python:
            prueba = SALIDA_DEFECTO / "_verificacion_fluke.csv"
            prueba.parent.mkdir(parents=True, exist_ok=True)
            prueba.unlink(missing_ok=True)
            r = subprocess.run([str(args.fluke_exe), "--port", args.fluke, "--once",
                                "--output", str(prueba)], capture_output=True, text=True, timeout=60)
            filas = prueba.read_text(encoding="utf-8-sig").splitlines()[1:] if prueba.exists() else []
            medidas = sorted({f.split('","')[6] for f in filas if f.count('","') > 6})
            print(f"Fluke {args.fluke}: {len(filas)} lecturas {medidas} {r.stdout.strip()[-120:]}")
            ok = ok and bool(filas)
    except Exception as exc:  # noqa: BLE001
        print(f"FALLO: {exc!r}")
        ok = False
    finally:
        for inst in (pcr, dmm):
            try:
                inst.cerrar()
            except Exception:  # noqa: BLE001
                pass
    print("BANCO LISTO" if ok else "REVISAR ANTES DE EJECUTAR")
    return 0 if ok else 2


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("escenario")
    ap.add_argument("--salida", type=Path, default=SALIDA_DEFECTO)
    ap.add_argument("--carga", default="", help="Descripcion de la carga conectada")
    ap.add_argument("--dut", default="SM-348518435344", help="device_id del medidor")
    ap.add_argument("--fw", default="1.1.0-metro-r1", help="Version de firmware cargada")
    ap.add_argument("--operador", default="")
    ap.add_argument("--temp", default="", help="Temperatura ambiente en C")
    ap.add_argument("--notas", default="")
    ap.add_argument("--pcr", default=ins.PCR_VISA)
    ap.add_argument("--dmm", default=ins.DMM_VISA)
    ap.add_argument("--dmm-modo", choices=["ACV", "ACI"], default=None,
                    help="ACV: DMM en paralelo (V y f). ACI: DMM en serie como amperimetro")
    ap.add_argument("--dmm-rango", default=None,
                    help="ACV: auto|100|750 (auto=100 V hasta --umbral-100v). ACI: 1|10 A")
    ap.add_argument("--umbral-100v", type=float, default=115.0)
    ap.add_argument("--fluke", default=None, help="Puerto del Fluke 43B, p.ej. COM9")
    ap.add_argument("--fluke-exe", type=Path, default=FLUKE_EXE,
                    help="Registrador Fluke43BLogger.exe (por defecto el de tools/)")
    ap.add_argument("--fluke-python", action="store_true",
                    help="Leer el Fluke desde Python (pyserial) en vez del .exe")
    ap.add_argument("--periodo", type=float, default=1.0, help="Periodo de muestreo PCR+DMM [s]")
    ap.add_argument("--periodo-fluke", type=float, default=1.0)
    ap.add_argument("--i-lim", type=float, default=None, help="Limite de corriente del PCR [A]")
    ap.add_argument("--v-max", type=float, default=140.0, help="Limite superior de tension [V]")
    ap.add_argument("--rampa-s", type=float, default=2.0, help="Rampa al energizar [s]")
    ap.add_argument("--sin-sincronia", action="store_true", help="No anteponer el preambulo SYNC")
    ap.add_argument("--sin-pausas", action="store_true", help="No esperar ENTER en cambios de carga")
    ap.add_argument("--si", action="store_true", help="Omitir la lista de chequeo inicial")
    ap.add_argument("--simular", action="store_true", help="Instrumentos simulados")
    ap.add_argument("--carga-simulada", default="potencia:15:0.62")
    ap.add_argument("--rapido", type=float, default=1.0, help="Divide las duraciones (solo pruebas)")
    ap.add_argument("--verificar", action="store_true",
                    help="Solo comprobar PCR y 34450A (salida apagada) y salir")
    args = ap.parse_args(argv)

    esc = leer_escenario(args.escenario)
    args.dmm_modo = args.dmm_modo or esc.meta.get("dmm_modo", "ACV").upper()
    if args.i_lim is None:
        args.i_lim = float(esc.meta.get("i_lim_a", "1.0"))
    pasos = construir_pasos(esc, args)
    v_max, f_min, f_max = validar(pasos, args)
    prueba = esc.meta.get("prueba", Path(args.escenario).stem).replace(" ", "_")
    total = sum(p.estab_s + p.med_s for p in pasos)
    print(f"Escenario {esc.ruta.name}: {prueba}, {len(pasos)} pasos, ~{total / 60:.1f} min")
    print(f"DMM {args.dmm_modo}, limite I {args.i_lim} A, V max {v_max} V, f {f_min}-{f_max} Hz")

    if args.simular:
        banco = ins.BancoSimulado(carga=args.carga_simulada)
        pcr, dmm = ins.FuenteSimulada(banco), ins.MultimetroSimulado(banco)
        fluke = None
    else:
        pcr, dmm = ins.FuentePCR(args.pcr), ins.MultimetroDMM(args.dmm)
        fluke = ins.Fluke43B(args.fluke) if (args.fluke and args.fluke_python) else None
        if args.fluke and not args.fluke_python and not args.fluke_exe.exists():
            raise SystemExit(f"No existe {args.fluke_exe}; use --fluke-python")
    if args.verificar:
        return verificar_banco(args, pcr, dmm, v_max, f_min, f_max)

    if not (args.si or args.simular):
        print("\nLISTA DE CHEQUEO (ADE9153A no aislado):\n"
              "  1. USB del medidor DESCONECTADO del PC; medidor alimentado por cargador de pared.\n"
              "  2. Medidor conectado SOLO a la salida del PCR (nunca a la red).\n"
              "  3. Carga pasando por el canal de corriente del medidor"
              + (" y por el 34450A en serie (borne A, rango correcto).\n" if args.dmm_modo == "ACI"
                 else ".\n  4. 34450A en paralelo en bornes de tension del medidor.\n")
              + "  5. microSD insertada; OLED mostrando mediciones.")
        if input("Escriba SI para continuar: ").strip().upper() != "SI":
            print("Cancelado.")
            return 1

    carpeta = carpeta_corrida(args.salida, prueba)
    shutil.copy2(esc.ruta, carpeta / esc.ruta.name)

    config = {
        "herramienta": f"ejecutar_escenario.py {VERSION}",
        "prueba": prueba, "descripcion": esc.meta.get("descripcion", ""),
        "escenario": esc.ruta.name, "escenario_sha256": sha256(esc.ruta),
        "meta_escenario": esc.meta, "inicio_utc": ahora_iso(), "fin_utc": None,
        "estado": "en_curso", "simulado": args.simular,
        "carga": args.carga, "operador": args.operador, "temperatura_c": args.temp,
        "notas": args.notas, "dut": {"device_id": args.dut, "firmware": args.fw},
        "instrumentos": {}, "dmm_modo": args.dmm_modo, "dmm_rango": args.dmm_rango or "auto",
        "umbral_100v": args.umbral_100v, "periodo_s": args.periodo, "i_lim_a": args.i_lim,
        "v_max": args.v_max, "rampa_s": args.rampa_s, "rapido": args.rapido,
        "host": platform.node(), "python": sys.version.split()[0],
        "pasos": [asdict(p) for p in pasos],
    }
    ruta_cfg = carpeta / "configuracion.json"

    def guardar_cfg():
        ruta_cfg.write_text(json.dumps(config, indent=2, ensure_ascii=False), encoding="utf-8")

    estado = "error"
    corrida = None
    try:
        config["instrumentos"]["pcr"] = pcr.abrir()
        config["instrumentos"]["dmm"] = dmm.abrir()
        errores = pcr.configurar(min(150.0, v_max + 2.0), max(40.0, f_min - 2.0),
                                 min(70.0, f_max + 2.0), args.i_lim)
        if errores:
            raise RuntimeError(f"PCR rechazo la configuracion: {errores}")
        guardar_cfg()
        corrida = Corrida(args, esc, pasos, carpeta, pcr, dmm, fluke)
        corrida.log(f"inicio corrida {carpeta.name} PCR={config['instrumentos']['pcr']} "
                    f"DMM={config['instrumentos']['dmm']}")
        estado = corrida.ejecutar()
        if fluke is not None:
            config["instrumentos"]["fluke"] = fluke.identidad
        config["errores_muestreo"] = corrida.errores_muestreo
    except KeyboardInterrupt:
        estado = "interrumpida"
    finally:
        try:
            pcr.cerrar()
        except Exception as exc:  # noqa: BLE001
            print(f"ERROR cerrando PCR: {exc!r} -- APAGAR LA SALIDA MANUALMENTE")
        try:
            dmm.cerrar()
        except Exception:  # noqa: BLE001
            pass
        config["estado"] = estado
        config["fin_utc"] = ahora_iso()
        guardar_cfg()
        if corrida is not None:
            corrida.log(f"fin corrida: {estado}")
            corrida.log_fh.close()
        huellas = [f"{sha256(p)}  {p.name}" for p in sorted(carpeta.iterdir())
                   if p.is_file() and p.name != "sha256.txt"]
        (carpeta / "sha256.txt").write_text("\n".join(huellas) + "\n", encoding="utf-8")
    print(f"\nCorrida {estado}: {carpeta}")
    print("Siguiente: copiar smartmeter/history_v1.csv de la microSD y ejecutar\n"
          f"  python analizar_metrologia.py \"{carpeta}\" --sd <ruta>\\history_v1.csv")
    return 0 if estado == "completa" else 2


if __name__ == "__main__":
    sys.exit(main())
