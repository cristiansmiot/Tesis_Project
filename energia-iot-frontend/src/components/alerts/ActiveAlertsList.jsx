import { AlertTriangle } from 'lucide-react';

/**
 * Panel de alertas activas del dashboard.
 * Props: alertas = [{ id, deviceId, tipo, mensaje, tiempo }]
 */
const ActiveAlertsList = ({ alertas = [] }) => {
  return (
    <div className="bg-white rounded-xl border border-gray-200 p-6 h-full">
      <h3 className="text-lg font-semibold text-gray-800 mb-4">Alertas activas</h3>

      {alertas.length === 0 ? (
        <div className="flex items-center justify-center h-40 text-gray-400 text-sm">
          No hay alertas activas
        </div>
      ) : (
        <div className="space-y-3">
          {alertas.map((alerta, i) => (
            <div
              key={alerta.id || i}
              className="flex items-start gap-3 p-3 bg-red-50 rounded-lg border border-red-100"
            >
              <AlertTriangle className="w-5 h-5 text-red-500 shrink-0 mt-0.5" />
              <div className="min-w-0">
                <p className="text-sm font-semibold text-gray-800">{alerta.deviceId}</p>
                <p className="text-sm text-gray-600">{alerta.mensaje}</p>
                <p className="text-xs text-gray-400 mt-1">{alerta.tiempo}</p>
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
};

export default ActiveAlertsList;
