import { User, Mail, Shield } from 'lucide-react';
import { useAuth } from '../contexts/AuthContext';

const Perfil = () => {
  const { user } = useAuth();

  const campos = [
    { icon: User, label: 'Nombre completo', valor: `${user?.nombre || ''} ${user?.apellido || ''}` },
    { icon: Mail, label: 'Correo electrónico', valor: user?.email || 'N/A' },
    { icon: Shield, label: 'Rol', valor: user?.rol || 'N/A' },
  ];

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Perfil</h2>
        <p className="text-gray-500 text-sm mt-1">Información de tu cuenta</p>
      </div>

      <div className="bg-white rounded-xl border border-gray-200 p-6 max-w-xl">
        {/* Avatar */}
        <div className="flex items-center gap-4 mb-6 pb-6 border-b border-gray-200">
          <div className="w-16 h-16 bg-blue-600 rounded-full flex items-center justify-center text-white text-xl font-bold">
            {user?.iniciales || 'U'}
          </div>
          <div>
            <h3 className="text-lg font-semibold text-gray-800">
              {user?.nombre} {user?.apellido}
            </h3>
            <p className="text-sm text-gray-500">{user?.rol}</p>
          </div>
        </div>

        {/* Campos */}
        <div className="space-y-4">
          {campos.map(({ icon: Icon, label, valor }) => (
            <div key={label} className="flex items-center gap-3">
              <div className="p-2 bg-gray-100 rounded-lg">
                <Icon className="w-5 h-5 text-gray-500" />
              </div>
              <div>
                <p className="text-xs text-gray-500">{label}</p>
                <p className="text-sm font-medium text-gray-800">{valor}</p>
              </div>
            </div>
          ))}
        </div>

        <p className="text-xs text-gray-400 mt-6">
          La edición del perfil y cambio de contraseña estarán disponibles con la autenticación del backend (Fase 2).
        </p>
      </div>
    </div>
  );
};

export default Perfil;
