/**
 * Lógica de umbrales de voltaje según CREG 024 de 2015
 * Redes de distribución en Colombia — usuarios residenciales
 * Voltaje nominal: 110V (120V en algunas zonas)
 * Tolerancia: ±10% del valor nominal
 */

const VOLTAJE_NOMINAL = 120;
const TOLERANCIA_PCT = 10;

export const VOLTAJE_MIN = VOLTAJE_NOMINAL * (1 - TOLERANCIA_PCT / 100); // 108V
export const VOLTAJE_MAX = VOLTAJE_NOMINAL * (1 + TOLERANCIA_PCT / 100); // 132V

const SIN_AC = { estado: 'sin_ac', color: 'gray', label: 'Sin AC' };

/**
 * Evalúa el estado de un voltaje según CREG 024/2015.
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
