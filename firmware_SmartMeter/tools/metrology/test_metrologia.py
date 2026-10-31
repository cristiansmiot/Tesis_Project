"""Prueba de extremo a extremo con banco simulado (sin instrumentos).

    python -m unittest test_metrologia -v

Ejecuta el escenario de humo con instrumentos simulados, fabrica un
historico de microSD con errores conocidos (V +0,30 %, f +0,020 Hz) y un
reloj desconocido (RTC sin hora valida), y verifica que el analisis
recupere el offset de reloj y los errores.
"""

import csv
import json
import shutil
import tempfile
import unittest
from pathlib import Path

import numpy as np
import pandas as pd

import analizar_metrologia as am
import ejecutar_escenario as ee

AQUI = Path(__file__).resolve().parent
UPTIME_INICIO_S = 5321.7  # uptime del DUT al empezar la corrida
GANANCIA_V = 1.003
SESGO_F = 0.020


def offset_real(corrida):
    """t_pc = uptime + offset para el DUT sintetico."""
    t0 = pd.read_csv(corrida / "referencia.csv")["t_epoch"].min()
    return float(t0) - UPTIME_INICIO_S


def fabricar_sd(corrida, ruta_sd, periodo_s=0.5, time_valid=False, error_rtc_s=0.0):
    ref = pd.read_csv(corrida / "referencia.csv").sort_values("t_epoch")
    t_ref = ref["t_epoch"].to_numpy()
    OFFSET_REAL = offset_real(corrida)
    dmm = ref["dmm_valor"].astype(float).interpolate().bfill().to_numpy()
    f_ref = ref["f_set"].to_numpy()
    rng = np.random.default_rng(7)
    filas, seq = [], 0
    # Historia previa ajena a la corrida (mismo boot, 5 s, 120 V estable).
    for up in np.arange(5.0, t_ref[0] - OFFSET_REAL - 60.0, 5.0):
        seq += 1
        filas.append((up, 120.4, 60.0, 0.0))
    for t in np.arange(t_ref[0], t_ref[-1], periodo_s):
        seq += 1
        v = np.interp(t, t_ref, dmm) * GANANCIA_V + rng.normal(0, 0.01)
        f = np.interp(t, t_ref, f_ref) + SESGO_F if v > 20 else 0.0
        filas.append((t - OFFSET_REAL, v, f, 0.2))
    with open(ruta_sd, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(am.SD_COLUMNAS)
        for k, (up, v, f, i) in enumerate(filas, 1):
            ts = int(up + OFFSET_REAL + error_rtc_s) if time_valid else 0
            w.writerow([1, "SM-TEST", 77, k, ts, "", "true" if time_valid else "false",
                        int(up * 1000), "measurement", f"{v:.3f}", f"{i:.4f}", f"{v * i * 0.62:.3f}",
                        "0", "0", "0.6200", f"{f:.3f}", "1.0000", "", 0])


class PruebaExtremoAExtremo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="metro_"))
        rc = ee.main([str(AQUI / "escenarios" / "00_humo.csv"), "--simular", "--si",
                      "--sin-pausas", "--rapido", "10", "--periodo", "0.2",
                      "--rampa-s", "0.5", "--salida", str(cls.tmp)])
        assert rc == 0, "la corrida simulada no termino"
        cls.corrida = next(cls.tmp.iterdir())

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def _analizar(self, **kw):
        sd = self.tmp / "history_v1.csv"
        fabricar_sd(self.corrida, sd, **kw)
        self.assertEqual(am.main([str(self.corrida), "--sd", str(sd)]), 0)
        res = self.corrida / "resultados"
        return (json.loads((res / "alineacion.json").read_text(encoding="utf-8")),
                pd.read_csv(res / "puntos.csv"))

    def test_archivos_de_corrida(self):
        for nombre in ["referencia.csv", "pasos.csv", "configuracion.json", "eventos.log", "sha256.txt"]:
            self.assertTrue((self.corrida / nombre).exists(), nombre)
        pasos = pd.read_csv(self.corrida / "pasos.csv")
        self.assertEqual(list(pasos["tipo"]).count("SYNC"), len(ee.PREAMBULO_SYNC))
        cfg = json.loads((self.corrida / "configuracion.json").read_text(encoding="utf-8"))
        self.assertEqual(cfg["estado"], "completa")

    def test_alineacion_sin_rtc_y_errores(self):
        alin, puntos = self._analizar(time_valid=False)
        usado = alin["usados"][0]
        self.assertAlmostEqual(usado["offset_s"], offset_real(self.corrida), delta=0.6)
        self.assertIsNone(usado["error_rtc_s"])
        pv = puntos[(puntos["variable"] == "V") & (puntos["rol"] == "principal")]
        self.assertTrue((pv["referencia"] == "dmm_v").all())
        np.testing.assert_allclose(pv["e_medio"], (GANANCIA_V - 1) * 100, atol=0.06)
        pf = puntos[(puntos["variable"] == "f") & (puntos["rol"] == "principal")]
        np.testing.assert_allclose(pf["e_medio"], SESGO_F, atol=0.003)
        self.assertTrue(set(pv["decision"]) <= {"conforme", "indeterminado", "no conforme"})

    def test_reporta_error_del_rtc(self):
        alin, _ = self._analizar(time_valid=True, error_rtc_s=7.0)
        usado = alin["usados"][0]
        self.assertAlmostEqual(usado["offset_s"], offset_real(self.corrida), delta=0.6)
        self.assertAlmostEqual(usado["error_rtc_s"], 7.0, delta=0.7)

    def test_decision(self):
        self.assertEqual(am.decidir(0.2, 0.3, 1.0), "conforme")
        self.assertEqual(am.decidir(1.5, 0.3, 1.0), "no conforme")
        self.assertEqual(am.decidir(0.8, 0.3, 1.0), "indeterminado")


if __name__ == "__main__":
    unittest.main()
