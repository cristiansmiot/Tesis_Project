import { useState, useEffect } from 'react';
import { useOutletContext } from 'react-router-dom';
import { FileText } from 'lucide-react';
import { auditoriaAPI } from '../services/api';

const Auditoria = () => {
  const { refreshKey } = useOutletContext();
  const [registros, setRegistros] = useState([]);
  const [total, setTotal] = useState(0);
  const [cargando, setCargando] = useState(true);

  useEffect(() => {
    cargarRegistros();
  }, [refreshKey]);

  const cargarRegistros = async () => {
    setCargando(true);
    try {
      const res = await auditoriaAPI.listar({ limit: 50 });
      setRegistros(res.registros || []);
      setTotal(res.total || 0);
    } catch (err) {
      console.error('Error cargando auditoría:', err);
    } finally {
      setCargando(false);
    }
  };

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Auditoría</h2>
        <p className="text-gray-500 text-sm mt-1">
          Registro de actividades y cambios en el sistema ({total} registros)
        </p>
      </div>

      <div className="bg-white rounded-xl border border-gray-200 overflow-hidden">
        {cargando ? (
          <div className="p-8 text-center text-gray-400 text-sm">Cargando registros...</div>
        ) : registros.length === 0 ? (
          <div className="p-12 text-center">
            <FileText className="w-12 h-12 text-gray-300 mx-auto mb-4" />
            <p className="text-gray-400 text-sm">No hay registros de auditoría</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-b border-gray-200 bg-gray-50">
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Fecha</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Usuario</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Acción</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Recurso</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">ID Recurso</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">Detalles</th>
                  <th className="text-left py-3 px-4 font-semibold text-gray-600">IP</th>
                </tr>
              </thead>
              <tbody>
                {registros.map((r) => (
                  <tr key={r.id} className="border-b border-gray-100 hover:bg-gray-50">
                    <td className="py-3 px-4 text-gray-500 text-xs whitespace-nowrap">
                      {r.timestamp ? new Date(r.timestamp).toLocaleString('es-CO') : '—'}
                    </td>
                    <td className="py-3 px-4 text-gray-700 text-xs">{r.usuario_email || 'Sistema'}</td>
                    <td className="py-3 px-4">
                      <span className="px-2 py-1 bg-blue-50 text-blue-700 rounded text-xs font-medium">
                        {r.accion}
                      </span>
                    </td>
                    <td className="py-3 px-4 text-gray-600 text-xs">{r.recurso || '—'}</td>
                    <td className="py-3 px-4 text-gray-600 font-mono text-xs">{r.recurso_id || '—'}</td>
                    <td className="py-3 px-4 text-gray-500 text-xs max-w-xs truncate">
                      {r.detalles || '—'}
                    </td>
                    <td className="py-3 px-4 text-gray-400 text-xs">{r.ip_address || '—'}</td>
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

export default Auditoria;
