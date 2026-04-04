/**
 * SERVICIO API — Modulo centralizado de comunicacion con el backend FastAPI.
 *
 * Estructura:
 *  - fetchAPI(): funcion base que agrega token JWT, maneja errores 401 (sesion expirada)
 *    y parsea las respuestas JSON.
 *  - authAPI: Login, perfil, cambiar contrasena, y gestion de usuarios (RBAC).
 *  - dispositivosAPI: CRUD de medidores de energia.
 *  - medicionesAPI: Lecturas de energia (ultima, historico, resumen).
 *  - saludAPI: Metricas de salud del nodo ESP32 (RSSI, reconexiones, etc.).
 *  - eventosAPI: Alarmas y eventos (sobrevoltaje, subtension, desconexion).
 *  - comandosAPI: Envio de comandos MQTT al dispositivo.
 *  - auditoriaAPI: Registro de actividades del sistema.
 *  - dashboardAPI: Resumen agregado y consumo horario.
 *
 * Variable de entorno: VITE_API_URL (ej: https://backend.up.railway.app)
 * Si no esta definida, usa http://localhost:8000 para desarrollo local.
 */

const API_BASE_URL = import.meta.env.VITE_API_URL
  ? `${import.meta.env.VITE_API_URL}/api/v1`
  : 'http://localhost:8000/api/v1';

/**
 * Funcion base de fetch con autenticacion JWT automatica.
 * - Agrega el header Authorization: Bearer {token} si hay sesion activa.
 * - Si el backend responde 401, borra la sesion y redirige a /login.
 * - Parsea errores del backend y los lanza como Error con el mensaje del detalle.
 */
const fetchAPI = async (url, options = {}) => {
  const stored = localStorage.getItem('auth_user');
  const token = stored ? JSON.parse(stored)?.token : null;

  const headers = {
    'Content-Type': 'application/json',
    ...options.headers,
  };

  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const response = await fetch(url, { ...options, headers });

  if (response.status === 401) {
    // Token expirado o inválido
    localStorage.removeItem('auth_user');
    window.location.href = '/login';
    throw new Error('Sesión expirada');
  }

  if (!response.ok) {
    const error = await response.json().catch(() => ({}));
    throw new Error(error.detail || `Error ${response.status}`);
  }

  return response.json();
};

/**
 * AUTENTICACIÓN
 */
export const authAPI = {
  login: async (email, password) => {
    return fetchAPI(`${API_BASE_URL}/auth/login`, {
      method: 'POST',
      body: JSON.stringify({ email, password }),
    });
  },

  me: async () => {
    return fetchAPI(`${API_BASE_URL}/auth/me`);
  },

  cambiarPassword: async (passwordActual, passwordNuevo) => {
    return fetchAPI(`${API_BASE_URL}/auth/cambiar-password`, {
      method: 'POST',
      body: JSON.stringify({
        password_actual: passwordActual,
        password_nuevo: passwordNuevo,
      }),
    });
  },

  // Gestion de usuarios (solo super_admin)
  listarUsuarios: async () => {
    return fetchAPI(`${API_BASE_URL}/auth/usuarios`);
  },

  registrar: async (datos) => {
    return fetchAPI(`${API_BASE_URL}/auth/registro`, {
      method: 'POST',
      body: JSON.stringify(datos),
    });
  },

  cambiarRol: async (usuarioId, rol) => {
    return fetchAPI(`${API_BASE_URL}/auth/usuarios/${usuarioId}/rol`, {
      method: 'PATCH',
      body: JSON.stringify({ rol }),
    });
  },

  toggleActivo: async (usuarioId) => {
    return fetchAPI(`${API_BASE_URL}/auth/usuarios/${usuarioId}/activo`, {
      method: 'PATCH',
    });
  },

  asignarDispositivos: async (usuarioId, deviceIds) => {
    return fetchAPI(`${API_BASE_URL}/auth/usuarios/${usuarioId}/dispositivos`, {
      method: 'PUT',
      body: JSON.stringify({ device_ids: deviceIds }),
    });
  },
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
 * EVENTOS Y ALARMAS
 */
export const eventosAPI = {
  listar: async (params = {}) => {
    const searchParams = new URLSearchParams();
    if (params.device_id) searchParams.append('device_id', params.device_id);
    if (params.tipo) searchParams.append('tipo', params.tipo);
    if (params.severidad) searchParams.append('severidad', params.severidad);
    if (params.activo !== undefined) searchParams.append('activo', params.activo);
    if (params.skip) searchParams.append('skip', params.skip);
    if (params.limit) searchParams.append('limit', params.limit);
    return fetchAPI(`${API_BASE_URL}/eventos/?${searchParams}`);
  },

  activos: async (limit = 20) => {
    return fetchAPI(`${API_BASE_URL}/eventos/activos?limit=${limit}`);
  },

  reconocer: async (eventoId) => {
    return fetchAPI(`${API_BASE_URL}/eventos/${eventoId}/reconocer`, {
      method: 'PATCH',
    });
  },
};

/**
 * COMANDOS
 */
export const comandosAPI = {
  disponibles: async (deviceId) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/${deviceId}/comandos/disponibles`);
  },

  enviar: async (deviceId, comando, parametros = null) => {
    return fetchAPI(`${API_BASE_URL}/dispositivos/${deviceId}/comando`, {
      method: 'POST',
      body: JSON.stringify({ comando, parametros }),
    });
  },
};

/**
 * AUDITORÍA
 */
export const auditoriaAPI = {
  listar: async (params = {}) => {
    const searchParams = new URLSearchParams();
    if (params.usuario_email) searchParams.append('usuario_email', params.usuario_email);
    if (params.accion) searchParams.append('accion', params.accion);
    if (params.recurso) searchParams.append('recurso', params.recurso);
    if (params.skip) searchParams.append('skip', params.skip);
    if (params.limit) searchParams.append('limit', params.limit);
    return fetchAPI(`${API_BASE_URL}/auditoria/?${searchParams}`);
  },
};

/**
 * DASHBOARD
 */
export const dashboardAPI = {
  resumen: async () => {
    return fetchAPI(`${API_BASE_URL}/dashboard/resumen`);
  },

  consumoHorario: async (horas = 24) => {
    return fetchAPI(`${API_BASE_URL}/dashboard/consumo-horario?horas=${horas}`);
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

export default {
  authAPI, dispositivosAPI, medicionesAPI, saludAPI,
  eventosAPI, comandosAPI, auditoriaAPI, dashboardAPI, healthCheck,
};
