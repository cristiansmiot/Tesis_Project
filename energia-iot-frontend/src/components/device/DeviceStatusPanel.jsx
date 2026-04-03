import { Wifi, WifiOff, AlertTriangle, Clock, Signal, RefreshCw, Activity } from 'lucide-react';
import StatusBadge from '../common/StatusBadge';

/**
 * Panel de estado actual y salud rapida del nodo.
 * Similar al diseno Figma: Estado actual + Salud rapida
 *
 * Props: dispositivo, medicion, eventosActivos, saludData, metricasTransmision
 */
const DeviceStatusPanel = ({ dispositivo, medicion, eventosActivos = 0, saludData, metricasTransmision }) => {
  const estaOnline = dispositivo?.conectado ?? false;
  const ultimaLectura = medicion?.timestamp
    ? new Date(medicion.timestamp).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' })
    : 'N/A';
  const ultimaConexion = dispositivo?.ultima_conexion
    ? new Date(dispositivo.ultima_conexion).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' })
    : 'N/A';

  // Extraer metricas de salud
  const rssiAvg = metricasTransmision?.rssi?.stats?.avg_dbm;
  const reconexiones = metricasTransmision?.reconexiones?.total_actual;
  const rssiColor = rssiAvg == null ? 'gray'
    : rssiAvg >= -70 ? 'green'
    : rssiAvg >= -85 ? 'yellow'
    : 'red';

  // Ultimo heartbeat desde saludData
  const ultimoHeartbeat = saludData?.timestamp
    ? new Date(saludData.timestamp).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' })
    : ultimaConexion;

  return (
    <div className="space-y-4">
      {/* Estado actual */}
      <div className="bg-white rounded-xl border border-gray-200 p-5">
        <h4 className="text-base font-semibold text-gray-800 mb-4">Estado actual</h4>
        <div className="space-y-3">
          <div className="flex items-center justify-between py-2 border-b border-gray-100">
            <span className="text-sm text-gray-600 flex items-center gap-2">
              {estaOnline ? <Wifi className="w-4 h-4 text-green-500" /> : <WifiOff className="w-4 h-4 text-red-500" />}
              Estado del dispositivo
            </span>
            <StatusBadge label={estaOnline ? 'Online' : 'Offline'} color={estaOnline ? 'green' : 'red'} />
          </div>

          <div className="flex items-center justify-between py-2 border-b border-gray-100">
            <span className="text-sm text-gray-600 flex items-center gap-2">
              <AlertTriangle className="w-4 h-4 text-yellow-500" />
              Alarmas activas
            </span>
            <span className={`text-sm font-bold ${eventosActivos > 0 ? 'text-red-600' : 'text-green-600'}`}>
              {eventosActivos}
            </span>
          </div>

          <div className="flex items-center justify-between py-2">
            <span className="text-sm text-gray-600 flex items-center gap-2">
              <Clock className="w-4 h-4 text-blue-500" />
              Ultima lectura valida
            </span>
            <span className="text-sm font-medium text-gray-800">{ultimaLectura}</span>
          </div>
        </div>
      </div>

      {/* Salud rapida */}
      <div className="bg-white rounded-xl border border-gray-200 p-5">
        <h4 className="text-base font-semibold text-gray-800 mb-4">Salud rapida</h4>
        <div className="space-y-3">
          <div className="flex items-center justify-between py-2 border-b border-gray-100">
            <span className="text-sm text-gray-600 flex items-center gap-2">
              <Signal className="w-4 h-4 text-indigo-500" />
              RSSI
            </span>
            <span className={`text-sm font-bold text-${rssiColor}-600`}>
              {rssiAvg != null ? `${rssiAvg} dBm` : 'N/A'}
            </span>
          </div>

          <div className="flex items-center justify-between py-2 border-b border-gray-100">
            <span className="text-sm text-gray-600 flex items-center gap-2">
              <RefreshCw className="w-4 h-4 text-orange-500" />
              Reconexiones
            </span>
            <span className={`text-sm font-bold ${reconexiones > 3 ? 'text-red-600' : 'text-gray-800'}`}>
              {reconexiones ?? 'N/A'}
            </span>
          </div>

          <div className="flex items-center justify-between py-2">
            <span className="text-sm text-gray-600 flex items-center gap-2">
              <Activity className="w-4 h-4 text-green-500" />
              Ultimo heartbeat
            </span>
            <span className="text-sm font-medium text-gray-800">{ultimoHeartbeat}</span>
          </div>
        </div>
      </div>
    </div>
  );
};

export default DeviceStatusPanel;
