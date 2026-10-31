"""Analiza una corrida de metrologia: DUT (microSD) contra referencias.

Uso:
    python analizar_metrologia.py <carpeta_corrida> --sd E:\\smartmeter\\history_v1.csv
        [--fluke data\\fluke43b_measurements.csv] [--offset-s X] [--boot-id N]

Pasos:
 1. Lee referencia.csv/pasos.csv de la corrida y el historico de la microSD.
 2. Alinea el reloj del DUT con el del PC por correlacion cruzada enmascarada
    de V (y f cuando varia) sobre uptime_ms; funciona aun sin hora RTC valida.
    Si el RTC estaba en hora, reporta su error frente al PC.
 3. Empareja cada muestra del DUT dentro de la ventana de medicion de cada
    escalon con el promedio de las referencias en +/-1 s.
 4. Calcula por punto y variable (protocolo Obj. 4, secciones 5 y 9): media,
    error medio, desviacion, MAE, error maximo, IC 95 %, U = 2*raiz(uA2 +
    uRef2 + uRes2) y la decision conforme / no conforme / indeterminado.
 5. P03 (sin carga): acumulacion de energia y falsos positivos.
    P02 (energia): delta Wh del DUT contra la integral de P de referencia.

Salida en <carpeta_corrida>/resultados/: puntos.csv, pares.csv, p02.csv,
p03.csv, alineacion.json, resumen.md, resultados.xlsx y graficos/*.png.
Los datos crudos no se modifican.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd

AQUI = Path(__file__).resolve().parent
SD_COLUMNAS = ["schema", "device", "boot_id", "sequence", "timestamp_utc",
               "local_bogota", "time_valid", "uptime_ms", "type", "vrms",
               "irms_a", "p_w", "q_var", "s_va", "pf", "f_hz", "e_wh",
               "temp_c", "pq"]
DUT_COL = {"V": "vrms", "I": "irms_a", "P": "p_w", "PF": "pf", "f": "f_hz"}
FLUKE_MAPA = {
    "true_rms_voltage_channel_A": "fluke_v",
    "true_rms_current_channel_B": "fluke_i",
    "frequency_voltage_channel_A": "fluke_f",
    "frequency_input_A_over_B": "fluke_f",
    "real_power_input_A_over_B": "fluke_p",
    "apparent_power_input_A_over_B": "fluke_s",
    "total_power_factor_input_A_over_B": "fluke_pf",
}


# --------------------------------------------------------------------------
# Lectura
# --------------------------------------------------------------------------

def a_epoch(serie):
    t = pd.to_datetime(serie, utc=True, format="ISO8601")
    return (t - pd.Timestamp("1970-01-01", tz="UTC")) / pd.Timedelta(seconds=1)


def leer_sd(ruta, dut=None):
    filas = []
    with open(ruta, encoding="utf-8-sig", errors="replace") as fh:
        for linea in fh:
            linea = linea.strip()
            if not linea or linea.startswith("schema,"):
                continue
            partes = linea.split(",")
            if len(partes) != len(SD_COLUMNAS):
                continue  # linea truncada por corte de energia
            filas.append(partes)
    sd = pd.DataFrame(filas, columns=SD_COLUMNAS)
    for c in ["boot_id", "sequence", "timestamp_utc", "uptime_ms", "vrms", "irms_a",
              "p_w", "q_var", "s_va", "pf", "f_hz", "e_wh", "temp_c", "pq"]:
        sd[c] = pd.to_numeric(sd[c], errors="coerce")
    sd["time_valid"] = sd["time_valid"].str.lower().eq("true")
    sd = sd[sd["type"] == "measurement"]
    if dut:
        sd = sd[sd["device"] == dut]
    sd["up_s"] = sd["uptime_ms"] / 1000.0
    return sd.dropna(subset=["boot_id", "uptime_ms", "vrms"]).reset_index(drop=True)


def leer_fluke(ruta):
    # csv tolera una ultima linea truncada si el registrador se detuvo escribiendo.
    import csv as _csv
    with open(ruta, encoding="utf-8-sig", newline="") as fh:
        filas = list(_csv.reader(fh))
    if not filas:
        return pd.DataFrame(), {}
    cab = filas[0]
    fl = pd.DataFrame([f for f in filas[1:] if len(f) == len(cab)], columns=cab, dtype=str)
    fl = fl[fl["measurement"].isin(FLUKE_MAPA) & fl["value"].notna() & (fl["valid"] == "1")]
    if fl.empty:
        return pd.DataFrame(), {}
    fl["var"] = fl["measurement"].map(FLUKE_MAPA)
    fl["t"] = a_epoch(fl["timestamp_utc"])
    fl["val"] = pd.to_numeric(fl["value"], errors="coerce")
    fl["res"] = pd.to_numeric(fl["resolution"], errors="coerce")
    resoluciones = fl.groupby("var")["res"].median().to_dict()
    tabla = fl.pivot_table(index="t", columns="var", values="val", aggfunc="first").reset_index()
    return tabla.sort_values("t").reset_index(drop=True), resoluciones


def leer_corrida(carpeta):
    ref = pd.read_csv(carpeta / "referencia.csv")
    pasos = pd.read_csv(carpeta / "pasos.csv", dtype={"paso": str})
    cfg = json.loads((carpeta / "configuracion.json").read_text(encoding="utf-8"))
    ref = ref.sort_values("t_epoch").reset_index(drop=True)
    ref["t"] = ref["t_epoch"].astype(float)
    acv = ref["dmm_modo"].eq("ACV")
    ref["dmm_v"] = np.where(acv, ref["dmm_valor"], np.nan)
    ref["dmm_i"] = np.where(ref["dmm_modo"].eq("ACI"), ref["dmm_valor"], np.nan)
    ref["pcr_f_set"] = ref["f_set"]
    ref["pcr_pf"] = ref["pcr_pf"].abs()
    return ref, pasos, cfg


# --------------------------------------------------------------------------
# Alineacion de relojes
# --------------------------------------------------------------------------

def _xcorr(a, b, n):
    """c[k] = sum_j a[j+k] * b[j], k en indices circulares de longitud n."""
    return np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)


def ncc_enmascarada(x, mx, y, my):
    """Correlacion normalizada de x (DUT) contra y (ref) para todos los lags."""
    n = 1 << int(math.ceil(math.log2(len(x) + len(y))))
    x = np.where(mx, x - x[mx].mean(), 0.0)
    y = np.where(my, y - y[my].mean(), 0.0)
    mxf, myf = mx.astype(float), my.astype(float)
    nn = _xcorr(mxf, myf, n)
    sx = _xcorr(x, myf, n)
    sxx = _xcorr(x * x, myf, n)
    sy = _xcorr(mxf, y, n)
    syy = _xcorr(mxf, y * y, n)
    sxy = _xcorr(x, y, n)
    nn = np.rint(nn)
    eps_x = 1e-6 * (x[mx].var() + 1e-12)
    eps_y = 1e-6 * (y[my].var() + 1e-12)
    with np.errstate(invalid="ignore", divide="ignore"):
        vx = (sxx - sx * sx / nn) / nn
        vy = (syy - sy * sy / nn) / nn
        r = (sxy - sx * sy / nn) / nn / np.sqrt(vx * vy)
    # Lags con poco solape o una serie plana dan correlaciones espurias.
    validos = (nn >= 3) & (vx > eps_x) & (vy > eps_y) & np.isfinite(r)
    r = np.where(validos, np.clip(r, -1.0, 1.0), -1.0)
    k = np.arange(n)
    k[k > len(x) - 1] -= n
    return k, r, nn


def r_directo(up, dut_val, t_ref, ref_val, c):
    t = up + c
    m = (t >= t_ref[0]) & (t <= t_ref[-1])
    if m.sum() < 8:
        return -1.0, int(m.sum())
    yr = np.interp(t[m], t_ref, ref_val)
    if np.std(yr) < 1e-9 or np.std(dut_val[m]) < 1e-9:
        return -1.0, int(m.sum())
    return float(np.corrcoef(dut_val[m], yr)[0, 1]), int(m.sum())


def alinear_boot(b, ref, ventana_rtc_s=120.0):
    """Devuelve dict con offset c (t_pc = uptime_s + c), score y diagnostico."""
    t_ref = ref["t"].to_numpy()
    señales = [("vrms", "pcr_v")]
    if ref["pcr_f_set"].std() > 0.05:
        señales.append(("f_hz", "pcr_f_set"))
    up = b["up_s"].to_numpy()
    periodo = float(np.median(np.diff(up))) if len(up) > 2 else 5.0
    validos = b[b["time_valid"] & (b["timestamp_utc"] > 0)]
    c_rtc = float(np.median(validos["timestamp_utc"] - validos["up_s"]) + 0.5) if len(validos) else None

    # Rejilla de 1 s para la busqueda gruesa por FFT.
    ux0, ty0 = math.floor(up[0]), math.floor(t_ref[0])
    nx = int(math.ceil(up[-1])) - ux0 + 1
    ny = int(math.ceil(t_ref[-1])) - ty0 + 1
    if nx > 4_000_000:
        raise ValueError("boot demasiado largo para la rejilla de 1 s")
    r_total = None
    for col_dut, col_ref in señales:
        x, mx = np.zeros(nx), np.zeros(nx, bool)
        idx = np.rint(up - ux0).astype(int)
        x[idx], mx[idx] = b[col_dut].to_numpy(), True
        mx &= np.isfinite(x)
        y = np.interp(np.arange(ny) + ty0, t_ref, ref[col_ref].to_numpy())
        my = np.isfinite(y)
        k, r, nn = ncc_enmascarada(np.nan_to_num(x), mx, np.nan_to_num(y), my)
        # Cobertura: al menos 60 % del maximo solape posible entre DUT y corrida.
        r = np.where(nn >= max(8.0, 0.6 * nn.max()), r, -1.0)
        r_total = r if r_total is None else (r_total + r)
    r_total /= len(señales)
    c_k = ty0 - ux0 - k  # t_pc = uptime + c  <=>  x[j+k] alineado con y[j]
    if c_rtc is not None:
        r_total = np.where(np.abs(c_k - c_rtc) <= ventana_rtc_s, r_total, -1.0)
    mejor = int(np.argmax(r_total))
    c0, score0 = float(c_k[mejor]), float(r_total[mejor])

    # Refinamiento a 0,1 s con interpolacion directa.
    finos = np.arange(c0 - 2.0, c0 + 2.0001, 0.1)
    puntajes = []
    for c in finos:
        rs = [r_directo(up, b[cd].to_numpy(), t_ref, ref[cr].to_numpy(), c)[0] for cd, cr in señales]
        puntajes.append(np.mean(rs))
    j = int(np.argmax(puntajes))
    c_fino = float(finos[j])
    r_v, n_sol = r_directo(up, b["vrms"].to_numpy(), t_ref, ref["pcr_v"].to_numpy(), c_fino)
    return {"boot_id": int(b["boot_id"].iloc[0]), "offset_s": c_fino, "score": float(puntajes[j]),
            "score_grueso": score0, "r_v": r_v, "muestras_solapadas": n_sol,
            "periodo_dut_s": periodo, "rtc_valido": c_rtc is not None,
            "error_rtc_s": (c_rtc - c_fino) if c_rtc is not None else None,
            "senales": [s[0] for s in señales]}


def alinear(sd, ref, offset_manual=None, boot_id=None):
    resultados = []
    for bid, b in sd.groupby("boot_id"):
        if boot_id is not None and int(bid) != boot_id:
            continue
        b = b.sort_values("up_s")
        if len(b) < 10:
            continue
        if offset_manual is not None:
            resultados.append({"boot_id": int(bid), "offset_s": offset_manual, "score": None,
                               "manual": True})
            continue
        try:
            resultados.append(alinear_boot(b, ref))
        except ValueError as exc:
            print(f"boot {bid}: {exc}")
    utiles = [r for r in resultados if r.get("manual") or
              (r["score"] >= 0.8 and r["muestras_solapadas"] >= 20)]
    return utiles, resultados


# --------------------------------------------------------------------------
# Incertidumbre
# --------------------------------------------------------------------------

def semiamplitud(spec, lectura, resolucion=None):
    a = abs(lectura) * spec.get("pct_lectura", 0.0) / 100.0
    a += spec.get("rango", 0.0) * spec.get("pct_rango", 0.0) / 100.0
    a += spec.get("abs", 0.0)
    res = spec.get("resolucion", resolucion)
    a += spec.get("cuentas", 0) * (res or 0.0)
    return a


def clave_spec(ref_col, rango):
    if ref_col == "dmm_v":
        return f"dmm_acv_{int(rango)}"
    if ref_col == "dmm_i":
        return f"dmm_aci_{int(rango)}"
    if ref_col == "dmm_f":
        return "dmm_freq"
    return ref_col


def u_referencia(cfgref, ref_col, ref_media, rango, medias, res_fluke):
    """Incertidumbre estandar (unidades de la variable) y resolucion de la referencia."""
    specs = cfgref["especificaciones"]
    spec = specs.get(clave_spec(ref_col, rango))
    if spec is None:
        return math.nan, 0.0, True
    if "derivado_de" in spec:  # FP = P / (V I)
        rel2 = 0.0
        for c in spec["derivado_de"]:
            m = medias.get(c)
            if not m:
                return math.nan, 0.0, True
            rel2 += (semiamplitud(specs[c], m) / math.sqrt(3) / abs(m)) ** 2
        return abs(ref_media) * math.sqrt(rel2), 0.0, spec.get("verificar", True)
    res = spec.get("resolucion", res_fluke.get(ref_col, 0.0))
    return semiamplitud(spec, ref_media, res) / math.sqrt(3), res or 0.0, spec.get("verificar", True)


def t95(n):
    try:
        from scipy import stats
        return float(stats.t.ppf(0.975, n - 1))
    except Exception:  # noqa: BLE001
        return 1.96 + 2.4 / max(n - 1, 1)


def decidir(e_medio, U, L):
    if not (math.isfinite(e_medio) and math.isfinite(U)):
        return "indeterminado"
    if abs(e_medio) + U <= L:
        return "conforme"
    if abs(e_medio) - U > L:
        return "no conforme"
    return "indeterminado"


# --------------------------------------------------------------------------
# Emparejamiento y estadistica por punto
# --------------------------------------------------------------------------

def referencia_en(t_dut, t_ref, valores, semiventana=1.0, maximo=3.5):
    """Promedio de la referencia en +/-semiventana; si no hay, la mas cercana."""
    out = np.full(len(t_dut), np.nan)
    finitos = np.isfinite(valores)
    tr, vr = t_ref[finitos], valores[finitos]
    if len(tr) == 0:
        return out
    for i, t in enumerate(t_dut):
        a, b = np.searchsorted(tr, [t - semiventana, t + semiventana])
        if b > a:
            out[i] = vr[a:b].mean()
        else:
            j = int(np.clip(np.searchsorted(tr, t), 1, len(tr) - 1))
            j = j if abs(tr[j] - t) < abs(tr[j - 1] - t) else j - 1
            if abs(tr[j] - t) <= maximo:
                out[i] = vr[j]
    return out


def analizar_puntos(dut, ref, pasos, fluke, res_fluke, cfgref, guarda_s=1.0):
    puntos, pares = [], []
    t_ref = ref["t"].to_numpy()
    for _, p in pasos.iterrows():
        if p["tipo"] in ("SYNC", "P03", "P02"):
            continue
        a, b = p["t_medicion"] + guarda_s, p["t_fin"] - guarda_s
        d = dut[(dut["t"] >= a) & (dut["t"] <= b)]
        if d.empty:
            puntos.append({"paso": p["paso"], "etiqueta": p["etiqueta"], "tipo": p["tipo"],
                           "variable": "-", "decision": "sin datos del DUT"})
            continue
        r_win = ref[(ref["t"] >= a - 2) & (ref["t"] <= b + 2)]
        rango = r_win["dmm_rango"].mode().iloc[0] if r_win["dmm_rango"].notna().any() else math.nan
        medias = {c: float(r_win[c].mean()) for c in ["pcr_v", "pcr_i", "pcr_p"] if c in r_win}
        for var, cols in cfgref["asignacion"].items():
            lim = cfgref["limites"][var]
            principal = True
            for ref_col in cols:
                if ref_col.startswith("fluke"):
                    if fluke.empty or ref_col not in fluke:
                        continue
                    rv = referencia_en(d["t"].to_numpy(), fluke["t"].to_numpy(),
                                       fluke[ref_col].to_numpy(dtype=float), 1.5, 3.5)
                else:
                    if ref_col not in ref or ref[ref_col].notna().sum() == 0:
                        continue
                    rv = referencia_en(d["t"].to_numpy(), t_ref, ref[ref_col].to_numpy(dtype=float))
                if np.isfinite(rv).sum() == 0:
                    continue
                dv = d[DUT_COL[var]].to_numpy(dtype=float)
                if var == "PF":
                    dv, rv = np.abs(dv), np.abs(rv)
                nota = []
                if var == "P" and np.nanmean(dv) < 0 < np.nanmean(rv):
                    dv, nota = -dv, ["signo de P invertido en el DUT"]
                ok = np.isfinite(dv) & np.isfinite(rv)
                dv, rv, tt = dv[ok], rv[ok], d["t"].to_numpy()[ok]
                n = len(dv)
                r_media = float(rv.mean())
                fila = {"paso": p["paso"], "etiqueta": p["etiqueta"], "tipo": p["tipo"],
                        "v_set": p["v_set"], "f_set": p["f_set"], "variable": var,
                        "referencia": ref_col, "rol": "principal" if principal else "comparacion",
                        "n": n, "ref_media": r_media, "dut_media": float(dv.mean()),
                        "unidad_error": lim["unidad"], "L": lim["L"]}
                minimo = cfgref["minimo_referencia_relativa"].get(var)
                if lim["tipo"] == "rel" and minimo is not None and abs(r_media) < minimo:
                    fila.update({"decision": "no aplica (referencia bajo minimo)"})
                    puntos.append(fila)
                    principal = False
                    continue
                e = (dv - rv) / rv * 100.0 if lim["tipo"] == "rel" else dv - rv
                sd = float(e.std(ddof=1)) if n > 1 else math.nan
                uA = sd / math.sqrt(n) if n > 1 else math.nan
                uR, resR, verificar = u_referencia(cfgref, ref_col, r_media, rango, medias, res_fluke)
                res_d = cfgref["resolucion_dut"][var]
                uRes = math.sqrt((res_d / (2 * math.sqrt(3))) ** 2 + (resR / (2 * math.sqrt(3))) ** 2)
                if lim["tipo"] == "rel":
                    uR, uRes = uR / abs(r_media) * 100.0, uRes / abs(r_media) * 100.0
                U = 2.0 * math.sqrt(np.nan_to_num(uA) ** 2 + uR ** 2 + uRes ** 2)
                e_medio = float(e.mean())
                decision = decidir(e_medio, U, lim["L"])
                if n < 30:
                    nota.append("n<30")
                if verificar:
                    nota.append("especificacion de referencia por verificar")
                imax = int(np.argmax(np.abs(e)))
                fila.update({"e_medio": e_medio, "desv_std": sd, "MAE": float(np.abs(e).mean()),
                             "e_max": float(e[imax]), "IC95": t95(n) * sd / math.sqrt(n) if n > 1 else math.nan,
                             "uA": uA, "uRef": uR, "uRes": uRes, "U": U,
                             "dmm_rango": rango if ref_col.startswith("dmm") else "",
                             "decision": decision if principal else f"({decision})",
                             "notas": "; ".join(nota)})
                puntos.append(fila)
                for ti, di, ri, ei in zip(tt, dv, rv, e):
                    pares.append({"paso": p["paso"], "etiqueta": p["etiqueta"], "variable": var,
                                  "referencia": ref_col, "t_epoch": ti, "dut": di, "ref": ri, "error": ei})
                principal = False
    return pd.DataFrame(puntos), pd.DataFrame(pares)


def analizar_sin_carga(dut, ref, pasos, cfgref):
    filas = []
    umbral = cfgref["umbral_acumulacion_sin_carga_wh"]
    for _, p in pasos[pasos["tipo"] == "P03"].iterrows():
        d = dut[(dut["t"] >= p["t_medicion"]) & (dut["t"] <= p["t_fin"])].sort_values("t")
        r = ref[(ref["t"] >= p["t_medicion"]) & (ref["t"] <= p["t_fin"])]
        if len(d) < 2:
            filas.append({"paso": p["paso"], "etiqueta": p["etiqueta"], "resultado": "sin datos del DUT"})
            continue
        de = float(d["e_wh"].iloc[-1] - d["e_wh"].iloc[0])
        horas = float(d["t"].iloc[-1] - d["t"].iloc[0]) / 3600.0
        filas.append({"paso": p["paso"], "etiqueta": p["etiqueta"], "n": len(d), "horas": horas,
                      "delta_e_wh": de, "umbral_wh": umbral,
                      "muestras_i_mayor_0": int((d["irms_a"] > 0).sum()),
                      "muestras_p_mayor_0": int((d["p_w"].abs() > 0).sum()),
                      "dut_i_max_a": float(d["irms_a"].max()), "dut_p_max_w": float(d["p_w"].abs().max()),
                      "pcr_i_media_a": float(r["pcr_i"].mean()), "pcr_p_media_w": float(r["pcr_p"].mean()),
                      "resultado": "sin acumulacion espuria" if abs(de) <= umbral else "ACUMULACION ESPURIA"})
    return pd.DataFrame(filas)


def analizar_energia(dut, ref, pasos, cfgref):
    filas = []
    spec = cfgref["especificaciones"]["pcr_p"]
    lim = cfgref["limites"]["E"]
    for _, p in pasos[pasos["tipo"] == "P02"].iterrows():
        d = dut[(dut["t"] >= p["t_medicion"]) & (dut["t"] <= p["t_fin"])].sort_values("t")
        if len(d) < 2:
            filas.append({"paso": p["paso"], "etiqueta": p["etiqueta"], "decision": "sin datos del DUT"})
            continue
        t1, t2 = float(d["t"].iloc[0]), float(d["t"].iloc[-1])
        r = ref[(ref["t"] >= t1 - 2) & (ref["t"] <= t2 + 2)].dropna(subset=["pcr_p"])
        tg = np.clip(r["t"].to_numpy(), t1, t2)
        e_ref = float(np.trapezoid(r["pcr_p"].to_numpy(), tg) / 3600.0)
        e_dut = float(d["e_wh"].iloc[-1] - d["e_wh"].iloc[0])
        p_media = float(r["pcr_p"].mean())
        u_rel = semiamplitud(spec, p_media) / math.sqrt(3) / abs(p_media) * 100.0 if p_media else math.nan
        u_res = cfgref["resolucion_dut"]["E"] / (2 * math.sqrt(3)) / abs(e_ref) * 100.0 if e_ref else math.nan
        U = 2 * math.sqrt(u_rel ** 2 + u_res ** 2)
        err = (e_dut - e_ref) / e_ref * 100.0 if e_ref else math.nan
        filas.append({"paso": p["paso"], "etiqueta": p["etiqueta"], "duracion_h": (t2 - t1) / 3600.0,
                      "p_ref_media_w": p_media, "e_dut_wh": e_dut, "e_ref_wh": e_ref, "error_pct": err,
                      "U_pct": U, "L_pct": lim["L"], "decision": decidir(err, U, lim["L"]),
                      "notas": "referencia = integral de P del PCR (+/-2 % + 0,5 W)"})
    return pd.DataFrame(filas)


# --------------------------------------------------------------------------
# Graficos y reportes
# --------------------------------------------------------------------------

def graficar(carpeta, dut, ref, pasos, puntos, pares, cfgref):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as exc:  # noqa: BLE001
        print(f"sin graficos: {exc}")
        return []
    g = carpeta / "graficos"
    g.mkdir(exist_ok=True)
    t0 = ref["t"].iloc[0]
    hechos = []
    for var, col, refcol, unidad in [("V", "vrms", "pcr_v", "V"), ("f", "f_hz", "pcr_f_set", "Hz"),
                                     ("I", "irms_a", "pcr_i", "A"), ("P", "p_w", "pcr_p", "W")]:
        fig, ax = plt.subplots(figsize=(11, 4))
        ax.plot((ref["t"] - t0) / 60, ref[refcol], lw=1, color="#555", label=f"referencia ({refcol})")
        if var == "V" and ref["dmm_v"].notna().any():
            ax.plot((ref["t"] - t0) / 60, ref["dmm_v"], lw=0.8, color="#1f77b4", label="34450A")
        ax.plot((dut["t"] - t0) / 60, dut[col].abs() if var == "P" else dut[col], ".", ms=3,
                color="#d62728", label="DUT (microSD)")
        for _, p in pasos.iterrows():
            ax.axvspan((p["t_medicion"] - t0) / 60, (p["t_fin"] - t0) / 60,
                       color="#2ca02c" if p["tipo"] != "SYNC" else "#bbb", alpha=0.12, lw=0)
        ax.set_xlabel("tiempo desde inicio [min]")
        ax.set_ylabel(f"{var} [{unidad}]")
        ax.set_title(f"{var}: DUT frente a referencia (franjas = ventanas de medicion)")
        ax.legend(loc="best", fontsize=8)
        ax.grid(alpha=0.3)
        fig.tight_layout()
        fig.savefig(g / f"serie_{var}.png", dpi=130)
        plt.close(fig)
        hechos.append(f"serie_{var}.png")
    if not puntos.empty and "e_medio" in puntos:
        pr = puntos[(puntos["rol"] == "principal") & puntos["e_medio"].notna()]
        for var in pr["variable"].unique():
            q = pr[pr["variable"] == var]
            lim = cfgref["limites"][var]
            fig, ax = plt.subplots(figsize=(9, 4))
            x = np.arange(len(q))
            ax.errorbar(x, q["e_medio"], yerr=q["U"], fmt="o", capsize=4, label="error medio +/- U")
            ax.axhline(lim["L"], color="r", ls="--", lw=1, label=f"+/-L = {lim['L']} {lim['unidad']}")
            ax.axhline(-lim["L"], color="r", ls="--", lw=1)
            ax.axhline(0, color="k", lw=0.5)
            ax.set_xticks(x, q["etiqueta"], rotation=30, ha="right", fontsize=8)
            ax.set_ylabel(f"error {var} [{lim['unidad'] or 'abs'}]")
            ax.set_title(f"P01 {var}: error por punto (referencia {q['referencia'].iloc[0]})")
            ax.legend(fontsize=8)
            ax.grid(alpha=0.3)
            fig.tight_layout()
            fig.savefig(g / f"error_{var}.png", dpi=130)
            plt.close(fig)
            hechos.append(f"error_{var}.png")
    if not pares.empty:
        for var in ["V", "I", "P"]:
            q = pares[(pares["variable"] == var)]
            if q.empty:
                continue
            q = q[q["referencia"] == q["referencia"].iloc[0]]
            media, dif = (q["dut"] + q["ref"]) / 2, q["dut"] - q["ref"]
            sesgo, s = dif.mean(), dif.std()
            fig, ax = plt.subplots(figsize=(7, 4))
            ax.plot(media, dif, ".", ms=3)
            for y, ls in [(sesgo, "-"), (sesgo + 1.96 * s, "--"), (sesgo - 1.96 * s, "--")]:
                ax.axhline(y, color="r", ls=ls, lw=1)
            ax.set_xlabel(f"media (DUT, ref) {var}")
            ax.set_ylabel(f"DUT - ref {var}")
            ax.set_title(f"Bland-Altman {var} (sesgo {sesgo:.4g}, LoA +/-{1.96 * s:.3g})")
            ax.grid(alpha=0.3)
            fig.tight_layout()
            fig.savefig(g / f"bland_altman_{var}.png", dpi=130)
            plt.close(fig)
            hechos.append(f"bland_altman_{var}.png")
    return hechos


def fmt(x, nd=4):
    if isinstance(x, (float, np.floating)):
        return "" if not math.isfinite(x) else f"{x:.{nd}g}"
    return str(x)


def tabla_md(df, columnas):
    if df.empty:
        return "_sin filas_\n"
    cols = [c for c in columnas if c in df]
    lineas = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for _, r in df.iterrows():
        lineas.append("| " + " | ".join(fmt(r[c]) for c in cols) + " |")
    return "\n".join(lineas) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("corrida", type=Path)
    ap.add_argument("--sd", type=Path, required=True, help="history_v1.csv copiado de la microSD")
    ap.add_argument("--fluke", type=Path, default=None, help="CSV del Fluke (por defecto el de la corrida)")
    ap.add_argument("--fluke-espiras", type=int, default=1,
                    help="Vueltas del conductor por la pinza del Fluke (divide I, P y S)")
    ap.add_argument("--referencias", type=Path, default=AQUI / "referencias.json")
    ap.add_argument("--dut", default=None, help="Filtrar por device_id")
    ap.add_argument("--offset-s", type=float, default=None, help="Offset manual t_pc = uptime + X")
    ap.add_argument("--boot-id", type=int, default=None)
    ap.add_argument("--guarda-s", type=float, default=1.0)
    args = ap.parse_args(argv)

    carpeta = args.corrida
    salida = carpeta / "resultados"
    salida.mkdir(exist_ok=True)
    cfgref = json.loads(args.referencias.read_text(encoding="utf-8"))
    ref, pasos, cfg = leer_corrida(carpeta)
    sd = leer_sd(args.sd, args.dut or None)
    if sd.empty:
        raise SystemExit("El historico de la microSD no tiene filas de medicion")
    ruta_fluke = args.fluke or (carpeta / "fluke43b.csv")
    fluke, res_fluke = (leer_fluke(ruta_fluke) if ruta_fluke.exists() else (pd.DataFrame(), {}))
    if not fluke.empty:
        fluke = fluke[(fluke["t"] >= ref["t"].min() - 5) & (fluke["t"] <= ref["t"].max() + 5)].copy()
        # N vueltas del conductor por la pinza multiplican I, P y S por N.
        for c in ("fluke_i", "fluke_p", "fluke_s"):
            if c in fluke:
                fluke[c] = fluke[c] / args.fluke_espiras
        for c in ("fluke_i", "fluke_p"):
            if c in res_fluke:
                res_fluke[c] = res_fluke[c] / args.fluke_espiras

    utiles, todos = alinear(sd, ref, args.offset_s, args.boot_id)
    (salida / "alineacion.json").write_text(json.dumps({"usados": utiles, "evaluados": todos},
                                                       indent=2, default=float), encoding="utf-8")
    if not utiles:
        for r in todos:
            print(f"boot {r['boot_id']}: score {r.get('score')}, solape {r.get('muestras_solapadas')}")
        raise SystemExit("No se pudo alinear el DUT con la referencia (score < 0,8). "
                         "Revise que la SD corresponda a la corrida o use --offset-s.")
    partes = []
    for a in utiles:
        b = sd[sd["boot_id"] == a["boot_id"]].copy()
        b["t"] = b["up_s"] + a["offset_s"]
        partes.append(b)
        print(f"boot {a['boot_id']}: offset {a['offset_s']:.1f} s, score {a.get('score')}, "
              f"error RTC {a.get('error_rtc_s')}")
    dut = pd.concat(partes).sort_values("t")
    dut = dut[(dut["t"] >= ref["t"].min() - 10) & (dut["t"] <= ref["t"].max() + 10)].reset_index(drop=True)

    puntos, pares = analizar_puntos(dut, ref, pasos, fluke, res_fluke, cfgref, args.guarda_s)
    p03 = analizar_sin_carga(dut, ref, pasos, cfgref)
    p02 = analizar_energia(dut, ref, pasos, cfgref)
    puntos.to_csv(salida / "puntos.csv", index=False)
    pares.to_csv(salida / "pares.csv", index=False)
    p03.to_csv(salida / "p03.csv", index=False)
    p02.to_csv(salida / "p02.csv", index=False)
    graficos = graficar(salida, dut, ref, pasos, puntos, pares, cfgref)

    verificar = sorted(k for k, v in cfgref["especificaciones"].items() if v.get("verificar"))
    md = [f"# Resultados {cfg.get('prueba')} - {carpeta.name}\n",
          f"- Escenario: `{cfg.get('escenario')}` (SHA-256 {cfg.get('escenario_sha256', '')[:16]}...)",
          f"- Carga: {cfg.get('carga') or 'sin declarar'}; DUT {cfg.get('dut')}; estado de la corrida: {cfg.get('estado')}",
          f"- Inicio UTC {cfg.get('inicio_utc')}; fin UTC {cfg.get('fin_utc')}; simulado: {cfg.get('simulado')}",
          f"- Instrumentos: {cfg.get('instrumentos')}",
          f"- Alineacion: " + "; ".join(
              f"boot {a['boot_id']} offset {a['offset_s']:.1f} s score {fmt(a.get('score'), 3)}"
              + (f", error RTC {a['error_rtc_s']:+.1f} s" if a.get("error_rtc_s") is not None else ", RTC sin hora valida")
              for a in utiles),
          f"- Muestras DUT en la corrida: {len(dut)}"
          + (f"; Fluke con {args.fluke_espiras} vueltas por la pinza" if args.fluke_espiras > 1 else ""),
          "- Escalones sin datos del DUT: " + (", ".join(
              puntos.loc[puntos["decision"] == "sin datos del DUT", "etiqueta"]) if not puntos.empty else "") + "\n",
          "## P01 por punto (referencia principal)\n",
          tabla_md(puntos[puntos.get("rol", pd.Series(dtype=str)) == "principal"] if not puntos.empty else puntos,
                   ["etiqueta", "variable", "referencia", "n", "ref_media", "dut_media", "e_medio",
                    "desv_std", "MAE", "e_max", "U", "L", "decision", "notas"]),
          "\n## Comparaciones secundarias\n",
          tabla_md(puntos[puntos.get("rol", pd.Series(dtype=str)) == "comparacion"] if not puntos.empty else puntos,
                   ["etiqueta", "variable", "referencia", "n", "ref_media", "dut_media", "e_medio", "U", "decision"]),
          "\n## P03 sin carga\n", tabla_md(p03, list(p03.columns)),
          "\n## P02 energia\n", tabla_md(p02, list(p02.columns)),
          "\n## Notas\n",
          "- Error relativo en % para V, I, P y E; absoluto para FP y f. FP se compara en valor absoluto.",
          "- Regla de decision (protocolo 5.1): conforme si |e|+U <= L; no conforme si |e|-U > L; si no, indeterminado.",
          "- Decisiones entre parentesis: referencia secundaria, solo comparacion.",
          f"- Especificaciones marcadas por verificar: {', '.join(verificar) or 'ninguna'}.",
          f"- Graficos: {', '.join(graficos)}"]
    (salida / "resumen.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    resultado = {"corrida": carpeta.name, "prueba": cfg.get("prueba"), "alineacion": utiles,
                 "decisiones": puntos[["etiqueta", "variable", "referencia", "rol", "decision"]].to_dict("records")
                 if not puntos.empty else [],
                 "p03": p03.to_dict("records"), "p02": p02.to_dict("records")}
    (salida / "resultado.json").write_text(json.dumps(resultado, indent=2, default=float, ensure_ascii=False),
                                           encoding="utf-8")
    try:
        with pd.ExcelWriter(salida / "resultados.xlsx") as xw:
            puntos.to_excel(xw, sheet_name="Puntos", index=False)
            p03.to_excel(xw, sheet_name="P03_sin_carga", index=False)
            p02.to_excel(xw, sheet_name="P02_energia", index=False)
            pasos.to_excel(xw, sheet_name="Pasos", index=False)
            pd.DataFrame(utiles).to_excel(xw, sheet_name="Alineacion", index=False)
            pares.to_excel(xw, sheet_name="Pares", index=False)
    except Exception as exc:  # noqa: BLE001
        print(f"sin Excel: {exc}")
    print((salida / "resumen.md").read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
