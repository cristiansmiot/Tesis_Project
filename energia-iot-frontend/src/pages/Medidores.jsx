import { useState, useEffect, useMemo } from 'react';
import { Link, useOutletContext } from 'react-router-dom';
import { Search, Trash2, X } from 'lucide-react';
import StatusBadge from '../components/common/StatusBadge';
import ConfirmDialog from '../components/common/ConfirmDialog';
import { dispositivosAPI, medicionesAPI } from '../services/api';
import { evaluarConectividad } from '../utils/voltage';
import { useAuth } from '../contexts/AuthContext';

const Medidores = () => {
  const { refreshKey } = useOutletContext();
  const { esSuperAdmin } = useAuth();
  const [dispositivos, setDispositivos] = useState([]);
  const [mediciones, setMediciones] = useState({});
  const [salud, setSalud] = useState({});
  const [busqueda, setBusqueda] = useState('');
  const [filtroEstado, setFiltroEstado] = useState('todos');
  const [filtroUbicacion, setFiltroUbicacion] = useState('todas');
  const [filtroEtiqueta, setFiltroEtiqueta] = useState('todas');
  const [cargando, setCargando] = useState(true);
  const [mensaje, setMensaje] = useState('');
  const [confirmEliminar, setConfirmEliminar] = useState(null); // dispositivo a retirar

  useEffect(() => {
    cargarDatos();
  }, [refreshKey]);

  const cargarDatos = async () => {
    setCargando(true);
    try {
      const disps = await dispositivosAPI.listar();
      setDispositivos(disps);

      // Obtener última medición y salud de cada dispositivo en paralelo
      const [medRes, saludRes] = await Promise.all([
        Promise.allSettled(disps.map((d) => medicionesAPI.ultima(d.device_id).then((m) => [d.device_id, m]))),
        Promise.allSettled(disps.map((d) => dispositivosAPI.salud(d.device_id).then((s) => [d.device_id, s]))),
      ]);

      const medMap = {};
      medRes.forEach((r) => {
        if (r.status === 'fulfilled') medMap[r.value[0]] = r.value[1];
      });
      setMediciones(medMap);

      const saludMap = {};
      saludRes.forEach((r) => {
        if (r.status === 'fulfilled') saludMap[r.value[0]] = r.value[1];
      });
      setSalud(saludMap);
    } catch (err) {
      console.error('Error cargando medidores:', err);
    } finally {
      setCargando(false);
    }
  };

  const eliminarMedidor = async () => {
    const d = confirmEliminar;
    setConfirmEliminar(null);
    try {
      const res = await dispositivosAPI.eliminar(d.device_id);
      setMensaje(res.mensaje);
      await cargarDatos();
    } catch (err) {
      setMensaje(`Error: ${err.message}`);
    }
  };

  // Valores únicos para los filtros
  const ubicaciones = useMemo(() => {
    const set = new Set(dispositivos.map((d) => d.ubicacion).filter(Boolean));
    return Array.from(set).sort();
  }, [dispositivos]);

  const etiquetas = useMemo(() => {
    const set = new Set(dispositivos.map((d) => d.etiqueta).filter(Boolean));
    return Array.from(set).sort();
  }, [dispositivos]);

  // Filtrado
  const filtrados = useMemo(() => {
    return dispositivos.filter((d) => {
      const matchBusqueda =
        !busqueda ||
        d.nombre?.toLowerCase().includes(busqueda.toLowerCase()) ||
        d.device_id?.toLowerCase().includes(busqueda.toLowerCase());
      const matchEstado =
        filtroEstado === 'todos' ||
        (filtroEstado === 'online' && d.conectado) ||
        (filtroEstado === 'offline' && !d.conectado);
      const matchUbicacion = filtroUbicacion === 'todas' || d.ubicacion === filtroUbicacion;
      const matchEtiqueta = filtroEtiqueta === 'todas' || d.etiqueta === filtroEtiqueta;
      return matchBusqueda && matchEstado && matchUbicacion && matchEtiqueta;
    });
  }, [dispositivos, busqueda, filtroEstado, filtroUbicacion, filtroEtiqueta]);

  const limpiarFiltros = () => {
    setBusqueda('');
    setFiltroEstado('todos');
    setFiltroUbicacion('todas');
    setFiltroEtiqueta('todas');
  };

  const tienesFiltros = busqueda || filtroEstado !== 'todos' || filtroUbicacion !== 'todas' || filtroEtiqueta !== 'todas';

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Medidores</h2>
        <p className="text-gray-500 text-sm mt-1">Lista general de dispositivos registrados</p>
      </div>

      {mensaje && (
        <div className={`mb-4 p-3 rounded-lg text-sm flex items-center gap-2 ${
          mensaje.startsWith('Error') ? 'bg-red-50 text-red-700' : 'bg-green-50 text-green-700'
        }`}>
          {mensaje}
          <button onClick={() => setMensaje('')} className="ml-auto"><X className="w-4 h-4" /></button>
        </div>
      )}

      {/* Filtros */}
      <div className="bg-white rounded-xl border border-gray-200 p-4 mb-6">
        <div className="flex flex-col md:flex-row gap-3 items-center">
          {/* Búsqueda */}
          <div className="relative flex-1 w-full">
            <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
            <input
              type="text"
              value={busqueda}
              onChange={(e) => setBusqueda(e.target.value)}
              placeholder="Buscar por nombre o ID"
              className="w-full pl-10 pr-4 py-2.5 border border-gray-300 rounded-lg text-sm focus:ring-2 focus:ring-blue-500 focus:border-blue-500 outline-none"
            />
          </div>

          {/* Filtro estado */}
          <select
            value={filtroEstado}
            onChange={(e) => setFiltroEstado(e.target.value)}
            className="px-4 py-2.5 border border-gray-300 rounded-lg text-sm bg-white focus:ring-2 focus:ring-blue-500 outline-none"
          >
            <option value="todos">Todos los estados</option>
            <option value="online">Online</option>
            <option value="offline">Offline</option>
          </select>

          {/* Filtro ubicación */}
          <select
            value={filtroUbicacion}
            onChange={(e) => setFiltroUbicacion(e.target.value)}
            className="px-4 py-2.5 border border-gray-300 rounded-lg text-sm bg-white focus:ring-2 focus:ring-blue-500 outline-none"
          >
            <option value="todas">Todas las ubicaciones</option>
            {ubicaciones.map((u) => (
              <option key={u} value={u}>{u}</option>
            ))}
          </select>

          {/* Filtro etiqueta (agrupación libre definida por el operador) */}
          {etiquetas.length > 0 && (
            <select
              value={filtroEtiqueta}
              onChange={(e) => setFiltroEtiqueta(e.target.value)}
              className="px-4 py-2.5 border border-gray-300 rounded-lg text-sm bg-white focus:ring-2 focus:ring-blue-500 outline-none"
            >
              <option value="todas">Todas las etiquetas</option>
              {etiquetas.map((t) => (
                <option key={t} value={t}>{t}</option>
              ))}
            </select>
          )}

          {tienesFiltros && (
            <button
              onClick={limpiarFiltros}
              className="flex items-center gap-1 px-4 py-2.5 text-sm text-gray-600 hover:text-gray-800 border border-gray-300 rounded-lg hover:bg-gray-50"
            >
              <X className="w-3.5 h-3.5" />
              Limpiar filtros
            </button>
          )}
        </div>
      </div>

      {/* Tabla */}
      <div className="bg-white rounded-xl border border-gray-200 overflow-hidden">
        <div className="px-6 py-4 border-b border-gray-200">
          <h3 className="font-semibold text-gray-800">Dispositivos ({filtrados.length})</h3>
        </div>

        {cargando ? (
          <div className="p-8 text-center text-gray-400 text-sm">Cargando dispositivos...</div>
        ) : filtrados.length === 0 ? (
          <div className="p-8 text-center text-gray-400 text-sm">No se encontraron dispositivos</div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-b border-gray-200 bg-gray-50">
                  <th className="text-left py-3 px-6 font-semibold text-gray-600">Nombre</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">ID</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Ubicación</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Etiqueta</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Estado</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Última lectura</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Energía acumulada</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Conectividad</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Acciones</th>
                </tr>
              </thead>
              <tbody>
                {filtrados.map((d) => {
                  const med = mediciones[d.device_id];
                  const s = salud[d.device_id];
                  const rssiStatus = evaluarConectividad(s?.rssi_dbm);
                  const ultimaLectura = med?.timestamp
                    ? new Date(med.timestamp).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' })
                    : '---';

                  return (
                    <tr key={d.device_id} className="border-b border-gray-100 hover:bg-gray-50">
                      <td className="py-3 px-6 font-medium text-gray-800">{d.nombre || d.device_id}</td>
                      <td className="py-3 px-4 text-gray-600 font-mono text-xs">{d.device_id}</td>
                      <td className="py-3 px-4 text-gray-600">{d.ubicacion || '—'}</td>
                      <td className="py-3 px-4">
                        {d.etiqueta ? (
                          <StatusBadge label={d.etiqueta} color="blue" showDot={false} />
                        ) : (
                          <span className="text-gray-400">—</span>
                        )}
                      </td>
                      <td className="py-3 px-4">
                        <StatusBadge
                          label={d.conectado ? 'Online' : 'Offline'}
                          color={d.conectado ? 'green' : 'red'}
                        />
                      </td>
                      <td className="py-3 px-4 text-gray-600">{ultimaLectura}</td>
                      <td className="py-3 px-4 font-medium text-gray-800">
                        {med?.energia_activa != null ? `${med.energia_activa.toFixed(1)} kWh` : '---'}
                      </td>
                      <td className="py-3 px-4">
                        <StatusBadge label={rssiStatus.nivel} color={rssiStatus.color} showDot={false} />
                      </td>
                      <td className="py-3 px-4">
                        <div className="flex items-center gap-3">
                          <Link
                            to={`/medidores/${d.device_id}`}
                            className="text-blue-600 hover:underline text-sm font-medium"
                          >
                            Ver detalle
                          </Link>
                          {esSuperAdmin && (
                            <button
                              onClick={() => setConfirmEliminar(d)}
                              title="Retirar medidor (el histórico se conserva)"
                              className="flex items-center gap-1 text-red-600 hover:text-red-700 text-xs font-medium"
                            >
                              <Trash2 className="w-3.5 h-3.5" />
                              Eliminar
                            </button>
                          )}
                        </div>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        )}
      </div>

      {/* Confirmación de retiro */}
      <ConfirmDialog
        open={!!confirmEliminar}
        title="Retirar medidor"
        message={`¿Retirar el medidor ${confirmEliminar?.nombre || confirmEliminar?.device_id}? Sus mediciones y eventos se conservan; si el equipo vuelve a reportar, se reactivará automáticamente con todo su histórico.`}
        confirmLabel="Retirar"
        cancelLabel="Cancelar"
        onConfirm={eliminarMedidor}
        onCancel={() => setConfirmEliminar(null)}
      />
    </div>
  );
};

export default Medidores;
