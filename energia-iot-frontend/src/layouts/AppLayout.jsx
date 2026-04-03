import { useState, useEffect, useCallback } from 'react';
import { Outlet, Navigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import Sidebar from '../components/sidebar/Sidebar';
import TopBar from '../components/topbar/TopBar';
import { healthCheck } from '../services/api';

const AppLayout = () => {
  const { isAuthenticated, loading } = useAuth();
  const [backendConectado, setBackendConectado] = useState(false);
  const [cargando, setCargando] = useState(false);
  const [refreshKey, setRefreshKey] = useState(0);

  const verificarConexion = useCallback(async () => {
    try {
      const health = await healthCheck();
      setBackendConectado(health.status === 'healthy');
    } catch {
      setBackendConectado(false);
    }
  }, []);

  useEffect(() => {
    verificarConexion();
    const interval = setInterval(verificarConexion, 30000);
    return () => clearInterval(interval);
  }, [verificarConexion]);

  const handleRefresh = () => {
    setCargando(true);
    setRefreshKey((k) => k + 1);
    verificarConexion().finally(() => setCargando(false));
  };

  if (loading) return null;
  if (!isAuthenticated) return <Navigate to="/login" replace />;

  return (
    <div className="flex h-screen overflow-hidden">
      <Sidebar />
      <div className="flex-1 flex flex-col overflow-hidden">
        <TopBar
          backendConectado={backendConectado}
          onRefresh={handleRefresh}
          cargando={cargando}
        />
        <main className="flex-1 overflow-y-auto bg-gray-50 p-6">
          <Outlet context={{ backendConectado, refreshKey }} />
        </main>
      </div>
    </div>
  );
};

export default AppLayout;
