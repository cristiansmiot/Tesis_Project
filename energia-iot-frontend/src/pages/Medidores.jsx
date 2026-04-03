import { useState, useEffect, useMemo } from 'react';
import { Link, useOutletContext } from 'react-router-dom';
import { Search, X } from 'lucide-react';
import StatusBadge from '../components/common/StatusBadge';
import { dispositivosAPI, medicionesAPI } from '../services/api';
import { evaluarConectividad } from '../utils/voltage';

const Medidores = () => {
  const { refreshKey } = useOutletContext();
  const [dispositivos, setDispositivos] = useState([]);
  const [mediciones, setMediciones] = useState({});
  const [salud, setSalud] = useState({});
  const [busqueda, setBusqueda] = useState('');
  const [filtroEstado, setFiltroEstado] = useState('todos');
  const [filtroUbicacion, setFiltroUbicacion] = useState('todas');
  const [cargando, setCargando] = useState(true);

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

  // Ubicaciones únicas para el filtro
  const ubicaciones = useMemo(() => {
    const set = new Set(dispositivos.map((d) => d.ubicacion).filter(Boolean));
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
      return matchBusqueda && matchEstado && matchUbicacion;
    });
  }, [dispositivos, busqueda, filtroEstado, filtroUbicacion]);

  const limpiarFiltros = () => {
    setBusqueda('');
    setFiltroEstado('todos');
    setFiltroUbicacion('todas');
  };

  const tienesFiltros = busqueda || filtroEstado !== 'todos' || filtroUbicacion !== 'todas';

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Medidores</h2>
        <p className="text-gray-500 text-sm mt-1">Lista general de dispositivos registrados</p>
      </div>

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
                        <Link
                          to={`/medidores/${d.device_id}`}
                          className="text-blue-600 hover:underline text-sm font-medium"
                        >
                          Ver detalle
                        </Link>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
};

export default Medidores;
