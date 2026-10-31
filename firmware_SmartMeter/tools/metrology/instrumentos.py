"""Controladores del banco de metrologia: PCR500MA, 34450A y Fluke 43B.

Cada instrumento real tiene un gemelo simulado con la misma interfaz para
ensayar escenarios y analisis sin banco (--simular).

Comandos tomados de los scripts ya probados en el banco (barrido_pcr_dmm.py,
registro_pcr_dmm.py) y del Keysight 34450A Programmer's Reference
(CONF:SEC:FREQ + READ? devuelve primaria y secundaria sin reconfigurar).
"""

from __future__ import annotations

import math
import random
import threading
import time

PCR_VISA = "USB0::0x0B3E::0x1050::AR002831::0::INSTR"
DMM_VISA = "USB0::0x0957::0xB318::MY52460013::0::INSTR"

# Indices de MEASure:ALL? (18 valores) usados en los scripts del banco.
PCR_IDX_I = 1     # CURRent:AC
PCR_IDX_P = 7     # POWer:AC:REAL
PCR_IDX_PF = 10   # POWer:AC:PFACtor
PCR_IDX_V = 16    # VOLTage:AC

DMM_OVERLOAD = 9.0e37


def _abrir_visa(recurso, timeout_ms=20000):
    import pyvisa  # importacion diferida: el modo simulado no la necesita

    rm = pyvisa.ResourceManager()
    inst = rm.open_resource(recurso)
    inst.timeout = timeout_ms
    inst.read_termination = "\n"
    inst.write_termination = "\n"
    return inst


def _errores_scpi(inst, maximo=10):
    errores = []
    for _ in range(maximo):
        try:
            resp = inst.query("SYSTem:ERRor?").strip()
        except Exception as exc:  # noqa: BLE001 - se reporta, no se oculta
            errores.append(f"SYST:ERR? fallo: {exc}")
            break
        if resp.startswith("+0") or resp.startswith("0,"):
            break
        errores.append(resp)
    return errores


class FuentePCR:
    """Kikusui PCR500MA por USB-TMC."""

    def __init__(self, recurso=PCR_VISA):
        self.recurso = recurso
        self.inst = None

    def abrir(self):
        self.inst = _abrir_visa(self.recurso)
        return self.inst.query("*IDN?").strip()

    def configurar(self, v_max, f_min, f_max, i_lim):
        w = self.inst.write
        w("OUTPut OFF")
        w("*CLS")
        w("OUTPut:COUPling AC")
        w("VOLTage:RANGe 155")
        w("VOLTage:LIMit:LOWer 0")
        w(f"VOLTage:LIMit:UPPer {v_max:.1f}")
        w(f"FREQuency:LIMit:LOWer {f_min:.2f}")
        w(f"FREQuency:LIMit:UPPer {f_max:.2f}")
        w(f"CURRent {i_lim:.2f}")
        return _errores_scpi(self.inst)

    def fijar(self, v, f):
        self.inst.write(f"FREQuency {f:.3f}")
        self.inst.write(f"VOLTage {v:.2f}")

    def fijar_tension(self, v):
        self.inst.write(f"VOLTage {v:.2f}")

    def salida(self, encendida):
        self.inst.write("OUTPut ON" if encendida else "OUTPut OFF")

    def medir(self):
        crudo = [float(x) for x in self.inst.query("MEASure:ALL?").split(",")]
        if len(crudo) != 18:
            raise RuntimeError(f"MEASure:ALL? devolvio {len(crudo)} valores")
        return {
            "pcr_v": crudo[PCR_IDX_V],
            "pcr_i": crudo[PCR_IDX_I],
            "pcr_p": crudo[PCR_IDX_P],
            "pcr_pf": crudo[PCR_IDX_PF],
            "pcr_crudo": ";".join(f"{x:.6g}" for x in crudo),
        }

    def errores(self):
        return _errores_scpi(self.inst)

    def cerrar(self):
        if self.inst is not None:
            try:
                self.inst.write("OUTPut OFF")
            finally:
                self.inst.close()
                self.inst = None


class MultimetroDMM:
    """Keysight 34450A: ACV o ACI primaria con frecuencia secundaria."""

    def __init__(self, recurso=DMM_VISA):
        self.recurso = recurso
        self.inst = None
        self.modo = None
        self.rango = None
        self.secundaria = False

    def abrir(self):
        self.inst = _abrir_visa(self.recurso)
        return self.inst.query("*IDN?").strip()

    def configurar(self, modo, rango, frecuencia_secundaria=True):
        """modo: 'ACV' (rango 100 o 750 V) o 'ACI' (rango 1 o 10 A)."""
        w = self.inst.write
        w("*RST")  # unica forma de apagar la funcion secundaria del 34450A
        w("*CLS")
        if modo == "ACV":
            w(f"CONFigure:VOLTage:AC {rango}")
        elif modo == "ACI":
            w(f"CONFigure:CURRent:AC {rango}")
        else:
            raise ValueError(f"modo DMM no soportado: {modo}")
        self.secundaria = False
        errores = []
        if frecuencia_secundaria:
            w("CONFigure:SECondary:FREQuency")
            errores = _errores_scpi(self.inst)
            self.secundaria = not errores
        self.inst.query("*OPC?")
        self.modo, self.rango = modo, rango
        errores += _errores_scpi(self.inst)
        return errores

    def leer(self):
        partes = [float(x) for x in self.inst.query("READ?").split(",")]
        valor = partes[0]
        f = partes[1] if (self.secundaria and len(partes) > 1) else math.nan
        if abs(valor) >= DMM_OVERLOAD:
            valor = math.nan  # sobrecarga (OL) en rango manual
        if abs(f) >= DMM_OVERLOAD:
            f = math.nan
        return valor, f

    def cerrar(self):
        if self.inst is not None:
            self.inst.close()
            self.inst = None


# --- Fluke 43B (puerto optico, protocolo del registrador Fluke43BLogger) ---

FLUKE_UNIDADES = {0: "none", 1: "V", 2: "A", 3: "ohm", 4: "W", 5: "F", 6: "K",
                  7: "s", 8: "h", 9: "day", 10: "Hz", 11: "degree", 12: "C",
                  13: "Fahrenheit", 14: "percent", 15: "dBm_50ohm",
                  16: "dBm_600ohm", 17: "dBV", 18: "dBA", 19: "dBW",
                  20: "VAR", 21: "VA"}
FLUKE_TIPOS = {0: "none", 1: "mean", 2: "rms", 3: "true_rms", 4: "peak_to_peak",
               5: "peak_max", 6: "peak_min", 7: "crest_factor", 8: "period",
               9: "duty_cycle_negative", 10: "duty_cycle_positive",
               11: "frequency", 12: "pulse_width_negative",
               13: "pulse_width_positive", 14: "phase", 15: "diode",
               16: "continuity", 18: "reactive_power", 19: "apparent_power",
               20: "real_power", 21: "harmonic_reactive_power",
               22: "harmonic_apparent_power", 23: "harmonic_real_power",
               24: "harmonic_rms", 25: "displacement_power_factor",
               26: "total_power_factor", 27: "total_harmonic_distortion",
               28: "thd_fundamental", 29: "k_factor_eu", 30: "k_factor_us",
               31: "line_frequency", 32: "ac_average"}
FLUKE_FUENTES = {1: "voltage_channel_A", 2: "current_channel_B",
                 3: "external_input", 12: "input_A_over_B",
                 21: "input_B_over_A"}
FLUKE_PRESENTACION = {0: "absolute", 1: "relative", 2: "logarithmic",
                      3: "linear", 4: "fahrenheit", 5: "celsius"}
FLUKE_CSV_HEADER = ["timestamp_local", "timestamp_utc", "instrument", "port",
                    "baud", "reading_number", "measurement", "source_code",
                    "source", "unit_code", "unit", "type_code", "type",
                    "presentation_code", "presentation", "resolution",
                    "raw_value", "value", "valid"]


class Fluke43B:
    """Lectura QM del Fluke 43B con conexion persistente."""

    def __init__(self, puerto="COM8"):
        self.puerto = puerto
        self.ser = None
        self.baud = None
        self.identidad = ""

    def abrir(self):
        try:
            import serial  # pyserial
        except ImportError as exc:
            raise RuntimeError(
                "Falta pyserial: python -m pip install pyserial "
                "(o use Fluke43BLogger.exe en paralelo)") from exc
        ultimo = None
        for baud in (19200, 9600, 4800, 2400, 1200):
            try:
                self.ser = serial.Serial(self.puerto, baud, bytesize=8,
                                         parity="N", stopbits=1, timeout=0.05,
                                         xonxoff=True, dsrdtr=False,
                                         rtscts=False)
                self.ser.dtr = False
                self.ser.rts = False
                time.sleep(0.15)
                self.ser.reset_input_buffer()
                resp = self._consulta("ID", 4.0)
                if len(resp) >= 2 and resp[0] == "0" and \
                        resp[1].upper().startswith("FLUKE 43"):
                    self.baud, self.identidad = baud, resp[1]
                    return self.identidad
            except Exception as exc:  # noqa: BLE001
                ultimo = exc
            self._cerrar_puerto()
            time.sleep(0.15)
        raise RuntimeError(f"Fluke sin respuesta en {self.puerto}: {ultimo}")

    def _consulta(self, comando, espera_max_s):
        self.ser.reset_input_buffer()
        self.ser.write((comando + "\r").encode("ascii"))
        recibido = b""
        limite = time.monotonic() + espera_max_s
        while time.monotonic() < limite:
            recibido += self.ser.read(256)
            lineas = recibido.split(b"\r")
            completas = [x for x in lineas[:-1] if x.strip()]
            if completas and completas[0].strip() != b"0":
                break  # ACK de error: no llega linea de datos
            if len(completas) >= 2:
                break
        if not recibido:
            raise TimeoutError(f"Fluke sin respuesta a {comando}")
        texto = recibido.decode("ascii", "replace").replace("\n", "")
        return [x for x in texto.split("\r") if x]

    def leer(self):
        """Devuelve [(descriptor, valor_crudo)] de las lecturas activas."""
        resp = self._consulta("QM", 5.0)
        if not resp or resp[0] != "0" or len(resp) < 2:
            raise RuntimeError(f"QM rechazado: {resp}")
        campos = resp[1].split(",")
        if len(campos) % 7 != 0:
            raise RuntimeError(f"descriptores QM inesperados: {resp[1]}")
        desc = []
        for k in range(0, len(campos), 7):
            desc.append({"numero": int(campos[k]), "valido": int(campos[k + 1]) == 1,
                         "fuente": int(campos[k + 2]), "unidad": int(campos[k + 3]),
                         "tipo": int(campos[k + 4]), "presentacion": int(campos[k + 5]),
                         "resolucion": campos[k + 6]})
        validos = [d for d in desc if d["valido"]]
        valores = {}
        for k in range(0, len(validos), 10):
            bloque = validos[k:k + 10]
            cmd = "QM " + ",".join(str(d["numero"]) for d in bloque)
            r = self._consulta(cmd, 5.0)
            if not r or r[0] != "0" or len(r) < 2:
                raise RuntimeError(f"{cmd} rechazado: {r}")
            vals = r[1].split(",")
            if len(vals) != len(bloque):
                raise RuntimeError(f"{cmd}: {len(vals)} valores para {len(bloque)}")
            for d, v in zip(bloque, vals):
                valores[d["numero"]] = v
        return [(d, valores.get(d["numero"], "")) for d in desc]

    def fila_csv(self, t_local, t_utc, d, crudo):
        try:
            valor = repr(float(crudo)) if crudo else ""
        except ValueError:
            valor = ""
        fuente = FLUKE_FUENTES.get(d["fuente"], f"code_{d['fuente']}")
        tipo = FLUKE_TIPOS.get(d["tipo"], f"code_{d['tipo']}")
        return [t_local, t_utc, self.identidad, self.puerto, str(self.baud),
                str(d["numero"]), f"{tipo}_{fuente}", str(d["fuente"]), fuente,
                str(d["unidad"]), FLUKE_UNIDADES.get(d["unidad"], f"code_{d['unidad']}"),
                str(d["tipo"]), tipo, str(d["presentacion"]),
                FLUKE_PRESENTACION.get(d["presentacion"], f"code_{d['presentacion']}"),
                d["resolucion"], crudo, valor, "1" if d["valido"] else "0"]

    def _cerrar_puerto(self):
        if self.ser is not None:
            try:
                self.ser.close()
            finally:
                self.ser = None

    def cerrar(self):
        self._cerrar_puerto()


# --- Banco simulado -------------------------------------------------------

class BancoSimulado:
    """Estado fisico compartido por los instrumentos simulados.

    carga: 'ninguna', 'resistiva:<ohm>' o 'potencia:<W>:<fp>' (driver LED).
    """

    def __init__(self, semilla=1, carga="potencia:15:0.62"):
        self.rng = random.Random(semilla)
        self.v_set = 0.0
        self.f_set = 60.0
        self.encendida = False
        self.carga = carga
        self.lock = threading.Lock()

    def verdad(self):
        with self.lock:
            if not self.encendida:
                return 0.0, self.f_set, 0.0, 0.0, 0.0
            v = self.v_set * 1.0004
            f = self.f_set
            tipo = self.carga.split(":")
            if tipo[0] == "resistiva":
                r = float(tipo[1])
                i, pf = v / r, 1.0
            elif tipo[0] == "potencia":
                p_nom, pf = float(tipo[1]), float(tipo[2])
                i = p_nom / (v * pf) if v > 20 else 0.0
            else:
                i, pf = 0.0012, 0.15
            return v, f, i, v * i * pf, pf


class FuenteSimulada:
    def __init__(self, banco):
        self.b = banco

    def abrir(self):
        return "SIMULADO,PCR500MA,0,0"

    def configurar(self, v_max, f_min, f_max, i_lim):
        return []

    def fijar(self, v, f):
        with self.b.lock:
            self.b.v_set, self.b.f_set = v, f

    def fijar_tension(self, v):
        with self.b.lock:
            self.b.v_set = v

    def salida(self, encendida):
        with self.b.lock:
            self.b.encendida = encendida

    def medir(self):
        v, f, i, p, pf = self.b.verdad()
        g = self.b.rng.gauss
        crudo = [0.0] * 18
        crudo[PCR_IDX_V] = v + g(0, 0.03)
        crudo[PCR_IDX_I] = i + 0.004 + g(0, 0.0005)
        crudo[PCR_IDX_P] = p + g(0, 0.05)
        crudo[PCR_IDX_PF] = pf
        time.sleep(0.01)
        return {"pcr_v": crudo[PCR_IDX_V], "pcr_i": crudo[PCR_IDX_I],
                "pcr_p": crudo[PCR_IDX_P], "pcr_pf": crudo[PCR_IDX_PF],
                "pcr_crudo": ";".join(f"{x:.6g}" for x in crudo)}

    def errores(self):
        return []

    def cerrar(self):
        self.salida(False)


class MultimetroSimulado:
    def __init__(self, banco):
        self.b = banco
        self.modo, self.rango, self.secundaria = None, None, True

    def abrir(self):
        return "SIMULADO,34450A,0,0"

    def configurar(self, modo, rango, frecuencia_secundaria=True):
        self.modo, self.rango, self.secundaria = modo, rango, frecuencia_secundaria
        return []

    def leer(self):
        v, f, i, _, _ = self.b.verdad()
        g = self.b.rng.gauss
        time.sleep(0.02)
        valor = (v + g(0, 0.01)) if self.modo == "ACV" else (i + g(0, 0.0002))
        if self.modo == "ACV" and self.rango == 100 and valor > 120.0:
            valor = math.nan
        return valor, (f + g(0, 0.0005)) if v > 1 else math.nan

    def cerrar(self):
        pass
