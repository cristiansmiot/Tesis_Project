/**
 * Umbrales residenciales de nivel 1 en Colombia.
 * Nominal: 120 V fase-tierra (RETIE 2024).
 * CREG: tensión estacionaria 90 %..110 % por más de un minuto.
 * Rango admisible: 108 V..132 V (Concepto CREG 6068 de 2025).
 * Estos límites deben coincidir con los del backend (services/mqtt_client.py),
 * que es quien genera los eventos persistidos; aquí solo se colorea la UI.
 */

const VOLTAJE_NOMINAL = 120;
const TOLERANCIA_PCT = 10;

export const VOLTAJE_MIN = Math.round(VOLTAJE_NOMINAL * (100 - TOLERANCIA_PCT)) / 100; // 108 V
export const VOLTAJE_MAX = Math.round(VOLTAJE_NOMINAL * (100 + TOLERANCIA_PCT)) / 100; // 132 V

const SIN_AC = { estado: 'sin_ac', color: 'gray', label: 'Sin AC' };

/**
 * Evalúa el estado instantáneo respecto a la banda CREG. La persistencia
 * superior a un minuto se valida en el backend antes de crear una alarma.
 * @param {number} voltaje - Voltaje RMS medido
 * @param {boolean|null} acPresente - Presencia de línea AC (saludData.ac_ok)
 * @returns {{ estado: string, color: string, label: string }}
 */
export function evaluarVoltaje(voltaje, acPresente = null) {
  if (acPresente === false) return SIN_AC;
  if (voltaje == null || isNaN(voltaje)) {
    return { estado: 'desconocido', color: 'gray', label: 'Sin dato' };
  }
  if (voltaje > VOLTAJE_MAX) {
    return { estado: 'sobrevoltaje', color: 'red', label: 'Sobrevoltaje' };
  }
  if (voltaje < VOLTAJE_MIN) {
    return { estado: 'subtension', color: 'red', label: 'Subtensión' };
  }
  return { estado: 'normal', color: 'green', label: 'Normal' };
}

/**
 * Evalúa el nivel de conectividad según RSSI
 * @param {number} rssi - RSSI en dBm
 * @returns {{ nivel: string, color: string }}
 */
export function evaluarConectividad(rssi) {
  if (rssi == null || rssi <= -127) return { nivel: 'Sin señal', color: 'gray' };
  if (rssi >= -70) return { nivel: 'Buena', color: 'green' };
  if (rssi >= -85) return { nivel: 'Media', color: 'yellow' };
  return { nivel: 'Baja', color: 'red' };
}

/**
 * Evalúa el factor de potencia.
 * @param {number} fp
 * @param {boolean|null} acPresente
 */
export function evaluarFactorPotencia(fp, acPresente = null) {
  if (acPresente === false) return SIN_AC;
  if (fp == null || isNaN(fp)) return { estado: 'desconocido', color: 'gray', label: 'Sin dato' };
  if (fp >= 0.9) return { estado: 'normal', color: 'green', label: 'Normal' };
  if (fp >= 0.8) return { estado: 'bajo', color: 'yellow', label: 'Bajo' };
  return { estado: 'critico', color: 'red', label: 'Crítico' };
}

/**
 * Evalúa frecuencia (nominal 60Hz en Colombia).
 * @param {number} freq
 * @param {boolean|null} acPresente
 */
export function evaluarFrecuencia(freq, acPresente = null) {
  if (acPresente === false) return SIN_AC;
  if (freq == null || isNaN(freq)) return { estado: 'desconocido', color: 'gray', label: 'Sin dato' };
  if (freq >= 59.5 && freq <= 60.5) return { estado: 'normal', color: 'green', label: 'Normal' };
  return { estado: 'anormal', color: 'red', label: 'Fuera de rango' };
}
