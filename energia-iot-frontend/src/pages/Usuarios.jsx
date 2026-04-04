/**
 * Pagina de Gestion de Usuarios — solo accesible para super_admin.
 *
 * Funcionalidades:
 *  - Listar todos los usuarios del sistema con su rol, estado y dispositivos.
 *  - Crear nuevos usuarios con seleccion de rol (super_admin, operador, visualizador).
 *  - Cambiar el rol de un usuario existente (dropdown en la tabla).
 *  - Activar/desactivar usuarios (no puede desactivarse a si mismo).
 *  - Asignar dispositivos a usuarios visualizador (modal con checkboxes).
 *
 * Proteccion: Si el usuario no es super_admin, se muestra mensaje de acceso denegado.
 * El backend tambien valida el rol en cada endpoint como segunda capa de seguridad.
 */
import { useState, useEffect } from 'react';
import { useOutletContext } from 'react-router-dom';
import { UserPlus, Shield, Power, HardDrive, X, Check, AlertTriangle, ShieldOff } from 'lucide-react';
import StatusBadge from '../components/common/StatusBadge';
import ConfirmDialog from '../components/common/ConfirmDialog';
import { authAPI, dispositivosAPI } from '../services/api';
import { useAuth } from '../contexts/AuthContext';

/** Roles disponibles en el sistema con su etiqueta y color para la UI */
const ROLES = [
  { value: 'super_admin', label: 'Super Admin', color: 'red' },
  { value: 'operador', label: 'Operador', color: 'blue' },
  { value: 'visualizador', label: 'Visualizador', color: 'gray' },
];

const Usuarios = () => {
  const { refreshKey } = useOutletContext();
  const { user, puedeGestionarUsuarios } = useAuth();

  // Verificacion de permisos en el cliente (el backend tambien valida)
  if (!puedeGestionarUsuarios) {
    return (
      <div className="flex flex-col items-center justify-center py-20">
        <ShieldOff className="w-16 h-16 text-gray-300 mb-4" />
        <h3 className="text-lg font-semibold text-gray-600">Acceso denegado</h3>
        <p className="text-sm text-gray-400 mt-1">Solo el Super Administrador puede gestionar usuarios.</p>
      </div>
    );
  }

  const [usuarios, setUsuarios] = useState([]);
  const [dispositivos, setDispositivos] = useState([]);
  const [cargando, setCargando] = useState(true);
  const [error, setError] = useState('');
  const [mensaje, setMensaje] = useState('');

  // Crear usuario
  const [mostrarFormulario, setMostrarFormulario] = useState(false);
  const [nuevoUsuario, setNuevoUsuario] = useState({
    email: '', nombre: '', apellido: '', password: '', rol: 'visualizador',
  });
  const [creando, setCreando] = useState(false);

  // Asignar dispositivos
  const [asignandoUsuario, setAsignandoUsuario] = useState(null);
  const [dispositivosSeleccionados, setDispositivosSeleccionados] = useState([]);

  // Confirm dialog
  const [confirmDialog, setConfirmDialog] = useState({ open: false, title: '', message: '', onConfirm: null });

  useEffect(() => {
    cargarDatos();
  }, [refreshKey]);

  const cargarDatos = async () => {
    setCargando(true);
    setError('');
    try {
      const [usrs, disps] = await Promise.all([
        authAPI.listarUsuarios(),
        dispositivosAPI.listar(),
      ]);
      setUsuarios(usrs);
      setDispositivos(disps);
    } catch (err) {
      setError(err.message);
    } finally {
      setCargando(false);
    }
  };

  const crearUsuario = async (e) => {
    e.preventDefault();
    setCreando(true);
    setError('');
    try {
      await authAPI.registrar(nuevoUsuario);
      setMensaje(`Usuario ${nuevoUsuario.email} creado exitosamente`);
      setMostrarFormulario(false);
      setNuevoUsuario({ email: '', nombre: '', apellido: '', password: '', rol: 'visualizador' });
      await cargarDatos();
    } catch (err) {
      setError(err.message);
    } finally {
      setCreando(false);
    }
  };

  const cambiarRol = async (usuarioId, nuevoRol) => {
    try {
      await authAPI.cambiarRol(usuarioId, nuevoRol);
      setMensaje('Rol actualizado');
      await cargarDatos();
    } catch (err) {
      setError(err.message);
    }
  };

  const toggleActivo = (usr) => {
    setConfirmDialog({
      open: true,
      title: usr.activo ? 'Desactivar usuario' : 'Activar usuario',
      message: `¿Esta seguro de ${usr.activo ? 'desactivar' : 'activar'} al usuario ${usr.email}?`,
      onConfirm: async () => {
        setConfirmDialog({ open: false, title: '', message: '', onConfirm: null });
        try {
          await authAPI.toggleActivo(usr.id);
          setMensaje(`Usuario ${usr.activo ? 'desactivado' : 'activado'}`);
          await cargarDatos();
        } catch (err) {
          setError(err.message);
        }
      },
    });
  };

  const abrirAsignacion = (usr) => {
    setAsignandoUsuario(usr);
    setDispositivosSeleccionados(usr.dispositivos_asignados || []);
  };

  const guardarAsignacion = async () => {
    try {
      await authAPI.asignarDispositivos(asignandoUsuario.id, dispositivosSeleccionados);
      setMensaje(`Dispositivos asignados a ${asignandoUsuario.email}`);
      setAsignandoUsuario(null);
      await cargarDatos();
    } catch (err) {
      setError(err.message);
    }
  };

  const toggleDispositivo = (deviceId) => {
    setDispositivosSeleccionados((prev) =>
      prev.includes(deviceId) ? prev.filter((id) => id !== deviceId) : [...prev, deviceId]
    );
  };

  const rolInfo = (rol) => ROLES.find((r) => r.value === rol) || { label: rol, color: 'gray' };

  return (
    <div>
      <div className="mb-6 flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-bold text-gray-800">Gestion de usuarios</h2>
          <p className="text-gray-500 text-sm mt-1">Administrar usuarios, roles y permisos</p>
        </div>
        <button
          onClick={() => setMostrarFormulario(true)}
          className="flex items-center gap-2 px-4 py-2.5 bg-blue-600 text-white text-sm font-medium rounded-lg hover:bg-blue-700 transition-colors"
        >
          <UserPlus className="w-4 h-4" />
          Crear usuario
        </button>
      </div>

      {/* Mensajes */}
      {error && (
        <div className="mb-4 p-3 rounded-lg bg-red-50 text-red-700 text-sm flex items-center gap-2">
          <AlertTriangle className="w-4 h-4 shrink-0" />
          {error}
          <button onClick={() => setError('')} className="ml-auto"><X className="w-4 h-4" /></button>
        </div>
      )}
      {mensaje && (
        <div className="mb-4 p-3 rounded-lg bg-green-50 text-green-700 text-sm flex items-center gap-2">
          <Check className="w-4 h-4 shrink-0" />
          {mensaje}
          <button onClick={() => setMensaje('')} className="ml-auto"><X className="w-4 h-4" /></button>
        </div>
      )}

      {/* Formulario crear usuario */}
      {mostrarFormulario && (
        <div className="bg-white rounded-xl border border-gray-200 p-6 mb-6">
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-base font-semibold text-gray-800">Nuevo usuario</h3>
            <button onClick={() => setMostrarFormulario(false)}>
              <X className="w-5 h-5 text-gray-400 hover:text-gray-600" />
            </button>
          </div>
          <form onSubmit={crearUsuario} className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">Email</label>
              <input
                type="email"
                required
                value={nuevoUsuario.email}
                onChange={(e) => setNuevoUsuario({ ...nuevoUsuario, email: e.target.value })}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none"
                placeholder="usuario@ejemplo.com"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">Nombre</label>
              <input
                type="text"
                required
                value={nuevoUsuario.nombre}
                onChange={(e) => setNuevoUsuario({ ...nuevoUsuario, nombre: e.target.value })}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">Apellido</label>
              <input
                type="text"
                required
                value={nuevoUsuario.apellido}
                onChange={(e) => setNuevoUsuario({ ...nuevoUsuario, apellido: e.target.value })}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">Contrasena</label>
              <input
                type="password"
                required
                minLength={6}
                value={nuevoUsuario.password}
                onChange={(e) => setNuevoUsuario({ ...nuevoUsuario, password: e.target.value })}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none"
                placeholder="Minimo 6 caracteres"
              />
            </div>
            <div>
              <label className="block text-sm font-medium text-gray-700 mb-1">Rol</label>
              <select
                value={nuevoUsuario.rol}
                onChange={(e) => setNuevoUsuario({ ...nuevoUsuario, rol: e.target.value })}
                className="w-full px-3 py-2 border border-gray-300 rounded-lg text-sm bg-white focus:ring-2 focus:ring-blue-500 outline-none"
              >
                {ROLES.map((r) => (
                  <option key={r.value} value={r.value}>{r.label}</option>
                ))}
              </select>
            </div>
            <div className="flex items-end">
              <button
                type="submit"
                disabled={creando}
                className="px-6 py-2 bg-blue-600 text-white text-sm font-medium rounded-lg hover:bg-blue-700 disabled:bg-gray-300 disabled:cursor-wait transition-colors"
              >
                {creando ? 'Creando...' : 'Crear usuario'}
              </button>
            </div>
          </form>
        </div>
      )}

      {/* Tabla de usuarios */}
      <div className="bg-white rounded-xl border border-gray-200 overflow-hidden">
        <div className="px-6 py-4 border-b border-gray-200">
          <h3 className="font-semibold text-gray-800">Usuarios ({usuarios.length})</h3>
        </div>

        {cargando ? (
          <div className="p-8 text-center text-gray-400 text-sm">Cargando usuarios...</div>
        ) : usuarios.length === 0 ? (
          <div className="p-8 text-center text-gray-400 text-sm">No hay usuarios registrados</div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-b border-gray-200 bg-gray-50">
                  <th className="text-left py-3 px-6 font-semibold text-gray-600">Usuario</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Email</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Rol</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Estado</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Dispositivos</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Acciones</th>
                </tr>
              </thead>
              <tbody>
                {usuarios.map((usr) => {
                  const ri = rolInfo(usr.rol);
                  const esMiUsuario = usr.id === user?.id;

                  return (
                    <tr key={usr.id} className="border-b border-gray-100 hover:bg-gray-50">
                      <td className="py-3 px-6">
                        <div className="flex items-center gap-3">
                          <div className="w-8 h-8 bg-blue-600 rounded-full flex items-center justify-center text-white text-xs font-bold">
                            {usr.iniciales || (usr.nombre?.[0] || '').toUpperCase()}
                          </div>
                          <span className="font-medium text-gray-800">
                            {usr.nombre} {usr.apellido}
                          </span>
                        </div>
                      </td>
                      <td className="py-3 px-4 text-gray-600">{usr.email}</td>
                      <td className="py-3 px-4">
                        {esMiUsuario ? (
                          <StatusBadge label={ri.label} color={ri.color} showDot={false} />
                        ) : (
                          <select
                            value={usr.rol}
                            onChange={(e) => cambiarRol(usr.id, e.target.value)}
                            className="px-2 py-1 border border-gray-300 rounded text-xs bg-white focus:ring-2 focus:ring-blue-500 outline-none"
                          >
                            {ROLES.map((r) => (
                              <option key={r.value} value={r.value}>{r.label}</option>
                            ))}
                          </select>
                        )}
                      </td>
                      <td className="py-3 px-4">
                        <StatusBadge
                          label={usr.activo ? 'Activo' : 'Inactivo'}
                          color={usr.activo ? 'green' : 'red'}
                        />
                      </td>
                      <td className="py-3 px-4">
                        {usr.rol === 'visualizador' ? (
                          <button
                            onClick={() => abrirAsignacion(usr)}
                            className="flex items-center gap-1 text-blue-600 hover:underline text-xs font-medium"
                          >
                            <HardDrive className="w-3.5 h-3.5" />
                            {usr.dispositivos_asignados?.length || 0} asignados
                          </button>
                        ) : (
                          <span className="text-gray-400 text-xs">Todos (por rol)</span>
                        )}
                      </td>
                      <td className="py-3 px-4">
                        {!esMiUsuario && (
                          <button
                            onClick={() => toggleActivo(usr)}
                            className={`flex items-center gap-1 text-xs font-medium ${
                              usr.activo ? 'text-red-600 hover:text-red-700' : 'text-green-600 hover:text-green-700'
                            }`}
                          >
                            <Power className="w-3.5 h-3.5" />
                            {usr.activo ? 'Desactivar' : 'Activar'}
                          </button>
                        )}
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Modal asignar dispositivos */}
      {asignandoUsuario && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50 p-4">
          <div className="bg-white rounded-xl w-full max-w-md max-h-[80vh] flex flex-col">
            <div className="px-6 py-4 border-b border-gray-200 flex items-center justify-between">
              <div>
                <h3 className="font-semibold text-gray-800">Asignar dispositivos</h3>
                <p className="text-xs text-gray-500 mt-1">
                  {asignandoUsuario.nombre} {asignandoUsuario.apellido} ({asignandoUsuario.email})
                </p>
              </div>
              <button onClick={() => setAsignandoUsuario(null)}>
                <X className="w-5 h-5 text-gray-400 hover:text-gray-600" />
              </button>
            </div>

            <div className="flex-1 overflow-y-auto p-4 space-y-2">
              {dispositivos.length === 0 ? (
                <p className="text-sm text-gray-400 text-center py-4">No hay dispositivos registrados</p>
              ) : (
                dispositivos.map((d) => (
                  <label
                    key={d.device_id}
                    className="flex items-center gap-3 p-3 rounded-lg border border-gray-200 hover:bg-gray-50 cursor-pointer"
                  >
                    <input
                      type="checkbox"
                      checked={dispositivosSeleccionados.includes(d.device_id)}
                      onChange={() => toggleDispositivo(d.device_id)}
                      className="w-4 h-4 text-blue-600 rounded border-gray-300 focus:ring-blue-500"
                    />
                    <div className="flex-1 min-w-0">
                      <p className="text-sm font-medium text-gray-800">{d.nombre || d.device_id}</p>
                      <p className="text-xs text-gray-500">{d.device_id} — {d.ubicacion || 'Sin ubicacion'}</p>
                    </div>
                    <StatusBadge
                      label={d.conectado ? 'Online' : 'Offline'}
                      color={d.conectado ? 'green' : 'red'}
                    />
                  </label>
                ))
              )}
            </div>

            <div className="px-6 py-4 border-t border-gray-200 flex items-center justify-between">
              <span className="text-xs text-gray-500">
                {dispositivosSeleccionados.length} de {dispositivos.length} seleccionados
              </span>
              <div className="flex gap-3">
                <button
                  onClick={() => setAsignandoUsuario(null)}
                  className="px-4 py-2 text-sm text-gray-700 border border-gray-300 rounded-lg hover:bg-gray-50"
                >
                  Cancelar
                </button>
                <button
                  onClick={guardarAsignacion}
                  className="px-4 py-2 text-sm text-white bg-blue-600 rounded-lg hover:bg-blue-700 font-medium"
                >
                  Guardar
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Confirm Dialog */}
      <ConfirmDialog
        open={confirmDialog.open}
        title={confirmDialog.title}
        message={confirmDialog.message}
        confirmLabel="Confirmar"
        cancelLabel="Cancelar"
        onConfirm={confirmDialog.onConfirm || (() => {})}
        onCancel={() => setConfirmDialog({ open: false, title: '', message: '', onConfirm: null })}
      />
    </div>
  );
};

export default Usuarios;
