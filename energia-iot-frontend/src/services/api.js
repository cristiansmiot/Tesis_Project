/**
 * API Service - Conexión con el backend FastAPI
 */

const API_BASE_URL = import.meta.env.VITE_API_URL
  ? `${import.meta.env.VITE_API_URL}/api/v1`
  : 'http://localhost:8000/api/v1';

/**
 * Fetch con headers de autenticación
 */
const fetchAPI = async (url, options = {}) => {
  const stored = localStorage.getItem('auth_user');
  const token = stored ? JSON.parse(stored)?.token : null;

  const headers = {
    'Content-Type': 'application/json',
    ...options.headers,
  };

  if (token && token !== 'mock-jwt-token') {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const response = await fetch(url, { ...options, headers });

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
  listar: async (skip = 0, limit = 100) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/?skip=${skip}&limit=${limit}`);
  },

  obtener: async (deviceId) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/${deviceId}`);
  },

  estado: async (deviceId) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/${deviceId}/estado`);
  },

  salud: async (deviceId) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/${deviceId}/salud`);
  },

  crear: async (datos) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/`, {
      method: 'POST',
      body: JSON.stringify(datos),
    });
  },

  actualizar: async (deviceId, datos) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/${deviceId}`, {
      method: 'PATCH',
      body: JSON.stringify(datos),
    });
  },
};

/**
 * MEDICIONES
 */
export const medicionesAPI = {
  listar: async (deviceId = null, limit = 100) => {
    const params = new URLSearchParams({ limit: limit.toString() });
    if (deviceId) params.append('device_id', deviceId);
    return fetchAPI(`${API_BASE_URL}/mediciones/?${params}`);
  },

  ultima: async (deviceId) => {
    return fetchAPI(`${API_BASE_URL}/mediciones/${deviceId}/ultima`);
  },

  resumen: async (deviceId) => {
    return fetchAPI(`${API_BASE_URL}/mediciones/${deviceId}/resumen`);
  },

  historico: async (deviceId, horas = 24) => {
    return fetchAPI(`${API_BASE_URL}/mediciones/${deviceId}/historico?horas=${horas}`);
  },
};

/**
 * SALUD DEL NODO
 */
export const saludAPI = {
  metricas: async (deviceId, horas = 24) => {
    return fetchAPI(`${API_BASE_URL}/salud/${deviceId}/metricas?horas=${horas}`);
  },

  historico: async (deviceId, horas = 24, limit = 100) => {
    return fetchAPI(`${API_BASE_URL}/salud/${deviceId}/historico?horas=${horas}&limit=${limit}`);
  },
};

/**
 * Health check del backend
 */
export const healthCheck = async () => {
  const baseUrl = import.meta.env.VITE_API_URL || 'http://localhost:8000';
  try {
    const response = await fetch(`${baseUrl}/health`);
    if (!response.ok) return { status: 'error' };
    return await response.json();
  } catch {
    return { status: 'error', message: 'No se puede conectar con el backend' };
  }
};

export default { dispositivosAPI, medicionesAPI, saludAPI, healthCheck };
