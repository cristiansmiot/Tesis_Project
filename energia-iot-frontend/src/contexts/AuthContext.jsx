/**
 * CONTEXTO DE AUTENTICACION — Proveedor global de estado de usuario y permisos.
 *
 * Funcionalidades:
 *  - Almacena el usuario autenticado en localStorage para persistir entre recargas.
 *  - Al iniciar, verifica el token con /auth/me y actualiza rol y dispositivos asignados
 *    (por si el super_admin cambio el rol del usuario desde otra sesion).
 *  - Expone funciones login() y logout() para iniciar y cerrar sesion.
 *  - Calcula helpers booleanos derivados del rol:
 *      esSuperAdmin, esOperador, esVisualizador,
 *      puedeEnviarComandos, puedeVerAuditoria, puedeGestionarUsuarios.
 *  - Estos helpers se usan en Sidebar, MedidorDetalle, Auditoria, Usuarios, etc.
 */
import { createContext, useContext, useState, useEffect } from 'react';
import { authAPI } from '../services/api';

const AuthContext = createContext(null);

export function AuthProvider({ children }) {
  const [user, setUser] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const stored = localStorage.getItem('auth_user');
    if (stored) {
      try {
        const parsed = JSON.parse(stored);
        setUser(parsed);
        // Verificar token y actualizar datos (rol puede haber cambiado)
        authAPI.me().then((data) => {
          const updated = { ...parsed, rol: data.rol, dispositivos_asignados: data.dispositivos_asignados };
          setUser(updated);
          localStorage.setItem('auth_user', JSON.stringify(updated));
        }).catch(() => {
          localStorage.removeItem('auth_user');
          setUser(null);
        });
      } catch {
        localStorage.removeItem('auth_user');
      }
    }
    setLoading(false);
  }, []);

  const login = async (email, password) => {
    if (!email || !password) {
      throw new Error('Correo y contraseña son requeridos');
    }

    const response = await authAPI.login(email, password);

    const userData = {
      id: response.usuario.id,
      nombre: response.usuario.nombre,
      apellido: response.usuario.apellido,
      email: response.usuario.email,
      rol: response.usuario.rol,
      iniciales: response.usuario.iniciales,
      token: response.access_token,
      dispositivos_asignados: response.usuario.dispositivos_asignados,
    };

    setUser(userData);
    localStorage.setItem('auth_user', JSON.stringify(userData));
    return userData;
  };

  const logout = () => {
    setUser(null);
    localStorage.removeItem('auth_user');
  };

  // Helpers de permisos derivados del rol
  const esSuperAdmin = user?.rol === 'super_admin';
  const esOperador = user?.rol === 'operador';
  const esVisualizador = user?.rol === 'visualizador';
  const puedeEnviarComandos = esSuperAdmin || esOperador;
  const puedeVerAuditoria = esSuperAdmin || esOperador;
  const puedeGestionarUsuarios = esSuperAdmin;

  return (
    <AuthContext.Provider value={{
      user,
      loading,
      login,
      logout,
      isAuthenticated: !!user,
      esSuperAdmin,
      esOperador,
      esVisualizador,
      puedeEnviarComandos,
      puedeVerAuditoria,
      puedeGestionarUsuarios,
    }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth() {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth debe usarse dentro de AuthProvider');
  return ctx;
}
