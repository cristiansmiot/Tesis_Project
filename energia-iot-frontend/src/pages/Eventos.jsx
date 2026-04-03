import { useState, useEffect } from 'react';
import { useOutletContext } from 'react-router-dom';
import { Bell, CheckCircle, AlertTriangle, Filter } from 'lucide-react';
import StatusBadge from '../components/common/StatusBadge';
import { eventosAPI } from '../services/api';

const severidadColor = {
  critical: 'red',
  warning: 'yellow',
  info: 'blue',
};

const Eventos = () => {
  const { refreshKey } = useOutletContext();
  const [eventos, setEventos] = useState([]);
  const [total, setTotal] = useState(0);
  const [filtroActivo, setFiltroActivo] = useState('todos');
  const [cargando, setCargando] = useState(true);

  useEffect(() => {
    cargarEventos();
  }, [refreshKey, filtroActivo]);

  const cargarEventos = async () => {
    setCargando(true);
    try {
      const params = { limit: 50 };
      if (filtroActivo === 'activos') params.activo = true;
      if (filtroActivo === 'reconocidos') params.activo = false;

      const res = await eventosAPI.listar(params);
      setEventos(res.eventos || []);
      setTotal(res.total || 0);
    } catch (err) {
      console.error('Error cargando eventos:', err);
    } finally {
      setCargando(false);
    }
  };

  const reconocer = async (eventoId) => {
    try {
      await eventosAPI.reconocer(eventoId);
      cargarEventos();
    } catch (err) {
      alert(`Error: ${err.message}`);
    }
  };

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Eventos y alarmas</h2>
        <p className="text-gray-500 text-sm mt-1">
          Historial de alertas y eventos del sistema
        </p>
      </div>

      {/* Filtros */}
      <div className="flex gap-2 mb-4">
        {['todos', 'activos', 'reconocidos'].map((f) => (
          <button
            key={f}
            onClick={() => setFiltroActivo(f)}
            className={`px-4 py-2 rounded-lg text-sm font-medium transition-colors ${
              filtroActivo === f
                ? 'bg-blue-100 text-blue-700'
                : 'bg-gray-100 text-gray-500 hover:bg-gray-200'
            }`}
          >
            {f.charAt(0).toUpperCase() + f.slice(1)} {f === 'todos' ? `(${total})` : ''}
          </button>
        ))}
      </div>

      {/* Tabla */}
      <div className="bg-white rounded-xl border border-gray-200 overflow-hidden">
        {cargando ? (
          <div className="p-8 text-center text-gray-400 text-sm">Cargando eventos...</div>
        ) : eventos.length === 0 ? (
          <div className="p-12 text-center">
            <Bell className="w-12 h-12 text-gray-300 mx-auto mb-4" />
            <p className="text-gray-400 text-sm">No hay eventos registrados</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-b border-gray-200 bg-gray-50">
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Dispositivo</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Tipo</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Severidad</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Mensaje</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Valor</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Fecha</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Estado</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Acciones</th>
                </tr>
              </thead>
              <tbody>
                {eventos.map((e) => (
                  <tr key={e.id} className="border-b border-gray-100 hover:bg-gray-50">
                    <td className="py-3 px-4 font-mono text-xs text-gray-700">{e.device_id}</td>
                    <td className="py-3 px-4 text-gray-600">{e.tipo}</td>
                    <td className="py-3 px-4">
                      <StatusBadge label={e.severidad} color={severidadColor[e.severidad] || 'gray'} showDot={false} />
                    </td>
                    <td className="py-3 px-4 text-gray-600 max-w-xs truncate">{e.mensaje}</td>
                    <td className="py-3 px-4 text-gray-600">
                      {e.valor != null ? `${e.valor.toFixed(1)}` : '—'}
                      {e.umbral != null ? ` / ${e.umbral.toFixed(0)}` : ''}
                    </td>
                    <td className="py-3 px-4 text-gray-500 text-xs">
                      {e.timestamp ? new Date(e.timestamp).toLocaleString('es-CO') : '—'}
                    </td>
                    <td className="py-3 px-4">
                      {e.activo ? (
                        <StatusBadge label="Activa" color="red" />
                      ) : (
                        <StatusBadge label="Reconocida" color="green" />
                      )}
                    </td>
                    <td className="py-3 px-4">
                      {e.activo && (
                        <button
                          onClick={() => reconocer(e.id)}
                          className="flex items-center gap-1 text-blue-600 hover:underline text-xs font-medium"
                        >
                          <CheckCircle className="w-3.5 h-3.5" />
                          Reconocer
                        </button>
                      )}
                      {!e.activo && e.reconocido_por && (
                        <span className="text-xs text-gray-400">{e.reconocido_por}</span>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
};

export default Eventos;
