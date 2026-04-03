import { useState } from 'react';
import { User, Mail, Shield, Lock } from 'lucide-react';
import { useAuth } from '../contexts/AuthContext';
import { authAPI } from '../services/api';

const Perfil = () => {
  const { user } = useAuth();
  const [showPassword, setShowPassword] = useState(false);
  const [passwordActual, setPasswordActual] = useState('');
  const [passwordNuevo, setPasswordNuevo] = useState('');
  const [mensaje, setMensaje] = useState('');
  const [error, setError] = useState('');

  const campos = [
    { icon: User, label: 'Nombre completo', valor: `${user?.nombre || ''} ${user?.apellido || ''}` },
    { icon: Mail, label: 'Correo electrónico', valor: user?.email || 'N/A' },
    { icon: Shield, label: 'Rol', valor: user?.rol || 'N/A' },
  ];

  const handleCambiarPassword = async (e) => {
    e.preventDefault();
    setMensaje('');
    setError('');
    try {
      await authAPI.cambiarPassword(passwordActual, passwordNuevo);
      setMensaje('Contraseña actualizada correctamente');
      setPasswordActual('');
      setPasswordNuevo('');
      setShowPassword(false);
    } catch (err) {
      setError(err.message);
    }
  };

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Perfil</h2>
        <p className="text-gray-500 text-sm mt-1">Información de tu cuenta</p>
      </div>

      <div className="max-w-xl space-y-6">
        {/* Info del usuario */}
        <div className="bg-white rounded-xl border border-gray-200 p-6">
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
        </div>

        {/* Cambiar contraseña */}
        <div className="bg-white rounded-xl border border-gray-200 p-6">
          <div className="flex items-center justify-between mb-4">
            <h4 className="font-semibold text-gray-800 flex items-center gap-2">
              <Lock className="w-4 h-4" />
              Cambiar contraseña
            </h4>
            {!showPassword && (
              <button
                onClick={() => setShowPassword(true)}
                className="text-sm text-blue-600 hover:underline"
              >
                Cambiar
              </button>
            )}
          </div>

          {showPassword && (
            <form onSubmit={handleCambiarPassword} className="space-y-3">
              {error && <p className="text-sm text-red-600 bg-red-50 p-2 rounded">{error}</p>}
              {mensaje && <p className="text-sm text-green-600 bg-green-50 p-2 rounded">{mensaje}</p>}
              <input
                type="password"
                placeholder="Contraseña actual"
                value={passwordActual}
                onChange={(e) => setPasswordActual(e.target.value)}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm outline-none focus:ring-2 focus:ring-blue-500"
                required
              />
              <input
                type="password"
                placeholder="Contraseña nueva"
                value={passwordNuevo}
                onChange={(e) => setPasswordNuevo(e.target.value)}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm outline-none focus:ring-2 focus:ring-blue-500"
                required
              />
              <div className="flex gap-2">
                <button
                  type="submit"
                  className="px-4 py-2 bg-blue-600 text-white rounded-lg text-sm font-medium hover:bg-blue-700"
                >
                  Guardar
                </button>
                <button
                  type="button"
                  onClick={() => { setShowPassword(false); setError(''); setMensaje(''); }}
                  className="px-4 py-2 bg-gray-100 text-gray-600 rounded-lg text-sm font-medium hover:bg-gray-200"
                >
                  Cancelar
                </button>
              </div>
            </form>
          )}
        </div>
      </div>
    </div>
  );
};

export default Perfil;
