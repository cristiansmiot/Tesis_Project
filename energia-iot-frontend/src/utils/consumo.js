/**
 * Consumo por intervalo a partir del contador acumulado del medidor.
 *
 * El firmware publica energia_activa como contador acumulado en Wh (NVS del
 * medidor). El consumo de un intervalo es la diferencia del contador entre el
 * ultimo valor de ese intervalo y el ultimo del intervalo anterior con datos.
 * Los intervalos se alinean a la hora local de Bogota (UTC-5, sin horario de
 * verano), que es la de la factura (kWh, Resolucion CREG 024 de 2015).
 */

const BOGOTA_OFFSET_MS = -5 * 3600 * 1000;
const HORA_MS = 3600 * 1000;
const DIA_MS = 24 * HORA_MS;

/** Granularidad de las barras segun el ancho de la ventana consultada. */
export const intervaloConsumo = (rangoMs) => {
  if (!Number.isFinite(rangoMs) || rangoMs <= 48 * HORA_MS) return 'hora';
  if (rangoMs <= 31.5 * DIA_MS) return 'dia';
  return 'mes';
};

/** Inicio (epoch ms real) del intervalo local de Bogota que contiene ms. */
export const inicioIntervalo = (ms, intervalo) => {
  const local = new Date(ms + BOGOTA_OFFSET_MS);
  const y = local.getUTCFullYear();
  const m = local.getUTCMonth();
  const d = local.getUTCDate();
  let inicioLocal;
  if (intervalo === 'hora') inicioLocal = Date.UTC(y, m, d, local.getUTCHours());
  else if (intervalo === 'dia') inicioLocal = Date.UTC(y, m, d);
  else inicioLocal = Date.UTC(y, m, 1);
  return inicioLocal - BOGOTA_OFFSET_MS;
};

/** Inicio del intervalo siguiente (los meses tienen largo variable). */
export const siguienteIntervalo = (inicioMs, intervalo) => {
  if (intervalo === 'hora') return inicioMs + HORA_MS;
  if (intervalo === 'dia') return inicioMs + DIA_MS;
  const local = new Date(inicioMs + BOGOTA_OFFSET_MS);
  return Date.UTC(local.getUTCFullYear(), local.getUTCMonth() + 1, 1) - BOGOTA_OFFSET_MS;
};

/**
 * @param {Array<{timestampMs:number, energia:number}>} puntos contador en Wh
 * @param {'hora'|'dia'|'mes'} intervalo
 * @returns {{barras: Array, resumen: object|null}}
 *   barras: {inicio, fin, wh, parcial, conHueco, reinicio, muestras}
 *   - parcial: primer intervalo de la ventana (solo cuenta desde su primera muestra)
 *   - conHueco: el intervalo anterior no tiene datos; la barra incluye ese periodo
 *   - reinicio: el contador bajo (reinicio de NVS); se usa solo el consumo interno
 */
export const calcularConsumo = (puntos, intervalo) => {
  const validos = (puntos || [])
    .filter((p) => Number.isFinite(p?.timestampMs) && Number.isFinite(p?.energia))
    .sort((a, b) => a.timestampMs - b.timestampMs);
  if (validos.length === 0) return { barras: [], resumen: null };

  const grupos = new Map();
  for (const p of validos) {
    const k = inicioIntervalo(p.timestampMs, intervalo);
    const g = grupos.get(k);
    if (g) {
      g.ultimo = p.energia;
      g.muestras += 1;
    } else {
      grupos.set(k, { inicio: k, primero: p.energia, ultimo: p.energia, muestras: 1 });
    }
  }

  const barras = [];
  let anterior = null;
  for (const k of [...grupos.keys()].sort((a, b) => a - b)) {
    const g = grupos.get(k);
    let wh;
    let parcial = false;
    let conHueco = false;
    let reinicio = false;
    if (anterior === null) {
      wh = g.ultimo - g.primero;
      parcial = true;
    } else {
      conHueco = siguienteIntervalo(anterior.inicio, intervalo) !== k;
      wh = g.ultimo - anterior.ultimo;
    }
    if (wh < 0) {
      reinicio = true;
      wh = Math.max(0, g.ultimo - g.primero);
    }
    barras.push({
      inicio: k,
      fin: siguienteIntervalo(k, intervalo),
      wh,
      parcial,
      conHueco,
      reinicio,
      muestras: g.muestras,
    });
    anterior = g;
  }

  const primero = validos[0];
  const ultimo = validos[validos.length - 1];
  const totalWh = barras.reduce((s, b) => s + b.wh, 0);
  const horas = (ultimo.timestampMs - primero.timestampMs) / HORA_MS;
  const promedioDiarioWh = horas >= 1 ? (totalWh / horas) * 24 : null;
  return {
    barras,
    resumen: {
      totalWh,
      horas,
      promedioDiarioWh,
      proyeccion30dWh: promedioDiarioWh === null ? null : promedioDiarioWh * 30,
      lecturaWh: ultimo.energia,
      desdeMs: primero.timestampMs,
      hastaMs: ultimo.timestampMs,
      reinicios: barras.filter((b) => b.reinicio).length,
      intervalosConHueco: barras.filter((b) => b.conHueco).length,
    },
  };
};

/** Texto de energia con la unidad adecuada: Wh bajo 1 kWh, kWh desde ahi. */
export const formatearEnergia = (wh, decimalesKwh = 3) => {
  if (!Number.isFinite(wh)) return '---';
  if (Math.abs(wh) >= 1000) {
    return `${(wh / 1000).toLocaleString('es-CO', {
      minimumFractionDigits: decimalesKwh,
      maximumFractionDigits: decimalesKwh,
    })} kWh`;
  }
  return `${wh.toLocaleString('es-CO', { minimumFractionDigits: 1, maximumFractionDigits: 1 })} Wh`;
};

/** Contador del medidor (Wh) expresado en kWh, unidad de facturacion. */
export const whAKwh = (wh, decimales = 3) =>
  (Number.isFinite(wh) ? (wh / 1000).toFixed(decimales) : null);

/**
 * Marcas regulares del eje de tiempo, alineadas a la hora local de Bogota:
 * 2 h (<=24 h), 4 h (<=48 h), 1 dia (<=8 dias), 5 dias (<=31 dias), mes.
 */
export const marcasTiempo = (inicioMs, finMs) => {
  if (!Number.isFinite(inicioMs) || !Number.isFinite(finMs) || finMs <= inicioMs) return undefined;
  const rango = finMs - inicioMs;
  let paso;
  let base;
  if (rango <= 24 * HORA_MS) { paso = 2 * HORA_MS; base = 'hora'; }
  else if (rango <= 48 * HORA_MS) { paso = 4 * HORA_MS; base = 'hora'; }
  else if (rango <= 8 * DIA_MS) { paso = DIA_MS; base = 'dia'; }
  else if (rango <= 31.5 * DIA_MS) { paso = 5 * DIA_MS; base = 'dia'; }
  else { paso = null; base = 'mes'; }

  const marcas = [];
  let t = inicioIntervalo(inicioMs, base);
  if (paso !== null && base === 'hora') {
    // Alinear a multiplos del paso en hora local (00:00, 02:00, ...).
    const horaLocal = new Date(t + BOGOTA_OFFSET_MS).getUTCHours();
    const pasoH = paso / HORA_MS;
    t += ((pasoH - (horaLocal % pasoH)) % pasoH) * HORA_MS;
  }
  if (t < inicioMs) {
    if (base === 'hora') t += paso;
    else t = siguienteIntervalo(t, base);
  }
  while (t <= finMs && marcas.length < 40) {
    marcas.push(t);
    t = paso !== null ? t + paso : siguienteIntervalo(t, 'mes');
  }
  return marcas;
};
