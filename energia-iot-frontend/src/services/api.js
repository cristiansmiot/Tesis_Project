/**
 * API Service - Conexión con el backend FastAPI
 */

// En desarrollo usa localhost; en producción usa la variable VITE_API_URL
const API_BASE_URL = import.meta.env.VITE_API_URL
  ? `${import.meta.env.VITE_API_URL}/api/v1`
  : 'http://localhost:8000/api/v1';

/**
 * Maneja errores de la API
 */
const handleResponse = async (response) => {
  if (!response.ok) {
    const error = await response.json().catch(() => ({}));
    throw new Error(error.detail || `Error ${response.status}`);
  }
  return response.json();
};

/**
 * DISPOSITIVOS
 */
export const dispositivosAPI = {
  // Listar todos los dispositivos
  listar: async () => {
    const response = await fetch(`${API_BASE_URL}/dispositivos/`);
    return handleResponse(response);
  },

  // Obtener un dispositivo por ID
  obtener: async (deviceId) => {
    const response = await fetch(`${API_BASE_URL}/dispositivos/${deviceId}`);
    return handleResponse(response);
  },

  // Obtener estado del dispositivo
  estado: async (deviceId) => {
    const response = await fetch(`${API_BASE_URL}/dispositivos/${deviceId}/estado`);
    return handleResponse(response);
  },

  // Obtener último registro de salud del nodo (indicadores + métricas de transmisión)
  salud: async (deviceId) => {
    const response = await fetch(`${API_BASE_URL}/dispositivos/${deviceId}/salud`);
    return handleResponse(response);
  },

  // Crear dispositivo
  crear: async (datos) => {
    const response = await fetch(`${API_BASE_URL}/dispositivos/`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(datos),
    });
    return handleResponse(response);
  },
};

/**
 * MEDICIONES
 */
export const medicionesAPI = {
  // Listar mediciones
  listar: async (deviceId = null, limit = 100) => {
    const params = new URLSearchParams({ limit: limit.toString() });
    if (deviceId) params.append('device_id', deviceId);
    const response = await fetch(`${API_BASE_URL}/mediciones/?${params}`);
    return handleResponse(response);
  },

  // Obtener última medición
  ultima: async (deviceId) => {
    const response = await fetch(`${API_BASE_URL}/mediciones/${deviceId}/ultima`);
    return handleResponse(response);
  },

  // Obtener resumen formateado
  resumen: async (deviceId) => {
    const response = await fetch(`${API_BASE_URL}/mediciones/${deviceId}/resumen`);
    return handleResponse(response);
  },

  // Obtener histórico con estadísticas
  historico: async (deviceId, horas = 24) => {
    const response = await fetch(`${API_BASE_URL}/mediciones/${deviceId}/historico?horas=${horas}`);
    return handleResponse(response);
  },

  // Crear medición (para pruebas)
  crear: async (datos) => {
    const response = await fetch(`${API_BASE_URL}/mediciones/`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(datos),
    });
    return handleResponse(response);
  },
};

/**
 * SALUD DEL NODO
 */
export const saludAPI = {
  // Obtener métricas de transmisión históricas + PDR
  metricas: async (deviceId, horas = 24) => {
    const response = await fetch(`${API_BASE_URL}/salud/${deviceId}/metricas?horas=${horas}`);
    return handleResponse(response);
  },

  // Obtener historial de registros de salud
  historico: async (deviceId, horas = 24, limit = 100) => {
    const response = await fetch(`${API_BASE_URL}/salud/${deviceId}/historico?horas=${horas}&limit=${limit}`);
    return handleResponse(response);
  },
};

/**
 * Health check del backend
 */
export const healthCheck = async () => {
  const baseUrl = import.meta.env.VITE_API_URL || 'http://localhost:8000';
  try {
    const response = await fetch(`${baseUrl}/health`);
    return await handleResponse(response);
  } catch (error) {
    return { status: 'error', message: error.message };
  }
};

export default { dispositivosAPI, medicionesAPI, saludAPI, healthCheck };
