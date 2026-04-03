import { NavLink } from 'react-router-dom';
import { Home, Gauge, Bell, FileText, User, LogOut, Cpu, Users } from 'lucide-react';
import clsx from 'clsx';
import { useAuth } from '../../contexts/AuthContext';

const ROL_LABELS = {
  super_admin: 'Super Admin',
  operador: 'Operador',
  visualizador: 'Visualizador',
};

const Sidebar = () => {
  const { user, logout, puedeVerAuditoria, puedeGestionarUsuarios } = useAuth();

  // Items de navegacion filtrados por rol
  const navItems = [
    { to: '/resumen', icon: Home, label: 'Resumen', visible: true },
    { to: '/medidores', icon: Gauge, label: 'Medidores', visible: true },
    { to: '/eventos', icon: Bell, label: 'Eventos y alarmas', visible: true },
    { to: '/auditoria', icon: FileText, label: 'Auditoría', visible: puedeVerAuditoria },
    { to: '/usuarios', icon: Users, label: 'Usuarios', visible: puedeGestionarUsuarios },
    { to: '/perfil', icon: User, label: 'Perfil', visible: true },
  ];

  const rolLabel = ROL_LABELS[user?.rol] || user?.rol;

  return (
    <aside className="w-64 bg-sidebar flex flex-col h-screen shrink-0">
      {/* Logo */}
      <div className="px-5 py-6 flex items-center gap-3">
        <div className="p-2 bg-blue-600 rounded-lg">
          <Cpu className="w-6 h-6 text-white" />
        </div>
        <div>
          <h1 className="text-white font-bold text-sm leading-tight">Medidor Inteligente</h1>
          <span className="text-blue-300 text-xs">IoT</span>
        </div>
      </div>

      {/* Navegacion */}
      <nav className="flex-1 px-3 mt-2 space-y-1">
        {navItems.filter((item) => item.visible).map(({ to, icon: Icon, label }) => (
          <NavLink
            key={to}
            to={to}
            className={({ isActive }) =>
              clsx(
                'flex items-center gap-3 px-3 py-2.5 rounded-lg text-sm font-medium transition-colors',
                isActive
                  ? 'bg-sidebar-hover text-white border-l-3 border-blue-400'
                  : 'text-gray-400 hover:bg-sidebar-hover hover:text-white'
              )
            }
          >
            <Icon className="w-5 h-5 shrink-0" />
            {label}
          </NavLink>
        ))}
      </nav>

      {/* Usuario */}
      <div className="px-4 py-4 border-t border-gray-700">
        <div className="flex items-center gap-3">
          <div className="w-8 h-8 bg-blue-600 rounded-full flex items-center justify-center text-white text-xs font-bold">
            {user?.iniciales || 'U'}
          </div>
          <div className="flex-1 min-w-0">
            <p className="text-white text-sm font-medium truncate">
              {user?.nombre} {user?.apellido}
            </p>
            <p className="text-gray-400 text-xs">{rolLabel}</p>
          </div>
        </div>
        <button
          onClick={logout}
          className="mt-3 flex items-center gap-2 text-gray-400 hover:text-white text-sm transition-colors w-full"
        >
          <LogOut className="w-4 h-4" />
          Cerrar sesion
        </button>
      </div>
    </aside>
  );
};

export default Sidebar;
