import { useLocation, Link } from 'react-router-dom';
import { RefreshCw, Wifi, WifiOff } from 'lucide-react';
import { useAuth } from '../../contexts/AuthContext';

const routeNames = {
  '/resumen': 'Resumen',
  '/medidores': 'Medidores',
  '/eventos': 'Eventos y alarmas',
  '/auditoria': 'Auditoría',
  '/perfil': 'Perfil',
};

const TopBar = ({ backendConectado, onRefresh, cargando }) => {
  const location = useLocation();
  const { user } = useAuth();

  // Generar breadcrumb
  const pathParts = location.pathname.split('/').filter(Boolean);
  const breadcrumbs = pathParts.map((part, index) => {
    const path = '/' + pathParts.slice(0, index + 1).join('/');
    const name = routeNames[path] || decodeURIComponent(part);
    return { path, name };
  });

  return (
    <header className="h-14 bg-white border-b border-gray-200 px-6 flex items-center justify-between shrink-0">
      {/* Breadcrumb */}
      <nav className="flex items-center gap-2 text-sm">
        {breadcrumbs.map((crumb, i) => (
          <span key={crumb.path} className="flex items-center gap-2">
            {i > 0 && <span className="text-gray-300">&gt;</span>}
            {i === breadcrumbs.length - 1 ? (
              <span className="text-gray-700 font-medium">{crumb.name}</span>
            ) : (
              <Link to={crumb.path} className="text-blue-600 hover:underline">
                {crumb.name}
              </Link>
            )}
          </span>
        ))}
      </nav>

      {/* Acciones */}
      <div className="flex items-center gap-4">
        {/* Estado conexión */}
        <div className={`flex items-center gap-1.5 px-3 py-1 rounded-full text-xs font-medium ${
          backendConectado
            ? 'bg-green-50 text-green-700'
            : 'bg-red-50 text-red-600'
        }`}>
          {backendConectado ? (
            <Wifi className="w-3.5 h-3.5" />
          ) : (
            <WifiOff className="w-3.5 h-3.5" />
          )}
          {backendConectado ? 'Conectado' : 'Desconectado'}
        </div>

        {/* Refresh */}
        <button
          onClick={onRefresh}
          disabled={cargando}
          className="p-2 text-gray-400 hover:text-gray-600 hover:bg-gray-100 rounded-lg transition-colors disabled:opacity-50"
        >
          <RefreshCw className={`w-4 h-4 ${cargando ? 'animate-spin' : ''}`} />
        </button>

        {/* Avatar */}
        <div className="w-8 h-8 bg-blue-600 rounded-full flex items-center justify-center text-white text-xs font-bold">
          {user?.iniciales || 'U'}
        </div>
      </div>
    </header>
  );
};

export default TopBar;
