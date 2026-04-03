import { createContext, useContext, useState, useEffect } from 'react';
import { authAPI } from '../services/api';

const AuthContext = createContext(null);

export function AuthProvider({ children }) {
  const [user, setUser] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    // Restaurar sesión desde localStorage
    const stored = localStorage.getItem('auth_user');
    if (stored) {
      try {
        const parsed = JSON.parse(stored);
        setUser(parsed);
        // Verificar que el token siga válido
        authAPI.me().catch(() => {
          // Token expirado, limpiar
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
    };

    setUser(userData);
    localStorage.setItem('auth_user', JSON.stringify(userData));
    return userData;
  };

  const logout = () => {
    setUser(null);
    localStorage.removeItem('auth_user');
  };

  return (
    <AuthContext.Provider value={{ user, loading, login, logout, isAuthenticated: !!user }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth() {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth debe usarse dentro de AuthProvider');
  return ctx;
}
