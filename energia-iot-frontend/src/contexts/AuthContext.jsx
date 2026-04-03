import { createContext, useContext, useState, useEffect } from 'react';

const AuthContext = createContext(null);

export function AuthProvider({ children }) {
  const [user, setUser] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    // Restaurar sesión desde localStorage
    const stored = localStorage.getItem('auth_user');
    if (stored) {
      try {
        setUser(JSON.parse(stored));
      } catch {
        localStorage.removeItem('auth_user');
      }
    }
    setLoading(false);
  }, []);

  // Mock login — Fase 2 conectará con POST /api/v1/auth/login
  const login = async (email, password) => {
    if (!email || !password) {
      throw new Error('Correo y contraseña son requeridos');
    }
    // Mock: cualquier credencial funciona
    const mockUser = {
      id: 1,
      nombre: 'Juan',
      apellido: 'Pérez',
      email,
      rol: 'Operador',
      iniciales: 'JP',
      token: 'mock-jwt-token',
    };
    setUser(mockUser);
    localStorage.setItem('auth_user', JSON.stringify(mockUser));
    return mockUser;
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
