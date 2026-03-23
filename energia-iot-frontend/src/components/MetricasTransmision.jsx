import { Signal, Radio, RefreshCw, BarChart3, CheckCircle, AlertTriangle } from 'lucide-react';

/**
 * Barra de progreso horizontal con color condicional.
 */
const BarraProgreso = ({ valor, max = 100, color = 'blue' }) => {
  const pct = Math.min((valor / max) * 100, 100);
  const colores = {
    green: 'bg-green-500',
    yellow: 'bg-yellow-500',
    red: 'bg-red-500',
    blue: 'bg-blue-500',
  };
  return (
    <div className="w-full bg-gray-200 rounded-full h-2">
      <div
        className={`h-2 rounded-full transition-all ${colores[color] || colores.blue}`}
        style={{ width: `${pct}%` }}
      />
    </div>
  );
};

/**
 * Califica el PDR y retorna color + icono.
 */
const calificarPDR = (pdr) => {
  if (pdr === null || pdr === undefined) return { color: 'gray', texto: 'N/A', icono: null };
  if (pdr >= 95) return { color: 'green', texto: 'Excelente', icono: CheckCircle };
  if (pdr >= 80) return { color: 'yellow', texto: 'Aceptable', icono: AlertTriangle };
  return { color: 'red', texto: 'Deficiente', icono: AlertTriangle };
};

/**
 * Califica el RSSI y retorna color descriptivo.
 */
const calificarRSSI = (dbm) => {
  if (dbm === null || dbm === undefined) return { color: 'gray', texto: 'N/A' };
  if (dbm >= -70) return { color: 'green', texto: 'Fuerte' };
  if (dbm >= -85) return { color: 'yellow', texto: 'Moderada' };
  if (dbm >= -100) return { color: 'red', texto: 'Débil' };
  return { color: 'red', texto: 'Muy débil' };
};

/**
 * Panel de métricas de transmisión del nodo ESP32.
 * Muestra PDR, RSSI, reconexiones y crecimiento de msg_tx.
 *
 * Props:
 *   metricas – objeto del endpoint /api/v1/salud/{device_id}/metricas
 */
const MetricasTransmision = ({ metricas }) => {
  if (!metricas) {
    return (
      <div className="bg-white rounded-xl shadow-md p-6">
        <h3 className="text-lg font-semibold text-gray-700 mb-2">📡 Métricas de Transmisión</h3>
        <p className="text-sm text-gray-400">Sin datos disponibles. El nodo aún no ha enviado /estado.</p>
      </div>
    );
  }

  const { pdr, rssi, reconexiones, msg_tx, ventana_horas } = metricas;
  const pdrCalif = calificarPDR(pdr?.porcentaje);
  const rssiCalif = calificarRSSI(rssi?.stats?.avg_dbm);

  return (
    <div className="bg-white rounded-xl shadow-md p-6 space-y-5">
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold text-gray-700">📡 Métricas de Transmisión</h3>
        <span className="text-xs text-gray-400">Últimas {ventana_horas}h</span>
      </div>

      {/* ── PDR (Packet Delivery Rate) ──────────────────────────────── */}
      <div className="bg-gray-50 rounded-lg p-4 space-y-2">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <BarChart3 className="w-5 h-5 text-indigo-500" />
            <span className="text-sm font-semibold text-gray-700">PDR</span>
          </div>
          <div className="flex items-center gap-2">
            {pdrCalif.icono && <pdrCalif.icono className={`w-4 h-4 text-${pdrCalif.color}-500`} />}
            <span className={`text-sm font-bold text-${pdrCalif.color}-600`}>
              {pdr?.porcentaje !== null ? `${pdr.porcentaje}%` : 'N/A'}
            </span>
          </div>
        </div>
        <BarraProgreso valor={pdr?.porcentaje || 0} color={pdrCalif.color} />
        <div className="flex justify-between text-xs text-gray-400">
          <span>{pdr?.mediciones_recibidas ?? 0} recibidas</span>
          <span>{pdr?.mediciones_esperadas ?? 0} esperadas</span>
        </div>
      </div>

      {/* ── RSSI ────────────────────────────────────────────────────── */}
      <div className="bg-gray-50 rounded-lg p-4 space-y-2">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Signal className="w-5 h-5 text-indigo-500" />
            <span className="text-sm font-semibold text-gray-700">Señal Celular (RSSI)</span>
          </div>
          <span className={`text-xs font-medium px-2 py-0.5 rounded-full bg-${rssiCalif.color}-100 text-${rssiCalif.color}-700`}>
            {rssiCalif.texto}
          </span>
        </div>
        {rssi?.stats ? (
          <div className="grid grid-cols-3 gap-3 text-center">
            <div>
              <p className="text-lg font-bold text-gray-800">{rssi.stats.avg_dbm}</p>
              <p className="text-xs text-gray-400">Prom. dBm</p>
            </div>
            <div>
              <p className="text-lg font-bold text-gray-800">{rssi.stats.min_dbm}</p>
              <p className="text-xs text-gray-400">Mín. dBm</p>
            </div>
            <div>
              <p className="text-lg font-bold text-gray-800">{rssi.stats.max_dbm}</p>
              <p className="text-xs text-gray-400">Máx. dBm</p>
            </div>
          </div>
        ) : (
          <p className="text-sm text-gray-400">Sin lecturas de RSSI</p>
        )}
      </div>

      {/* ── Contadores ──────────────────────────────────────────────── */}
      <div className="grid grid-cols-2 gap-3">
        <div className="bg-gray-50 rounded-lg p-3 space-y-1">
          <div className="flex items-center gap-2">
            <Radio className="w-4 h-4 text-blue-500" />
            <span className="text-xs font-semibold text-gray-500">Mensajes TX</span>
          </div>
          <p className="text-xl font-bold text-gray-800">{msg_tx?.total_actual ?? '—'}</p>
          {msg_tx?.delta_ventana !== null && msg_tx?.delta_ventana !== undefined && (
            <p className="text-xs text-green-600">+{msg_tx.delta_ventana} en {ventana_horas}h</p>
          )}
        </div>

        <div className="bg-gray-50 rounded-lg p-3 space-y-1">
          <div className="flex items-center gap-2">
            <RefreshCw className="w-4 h-4 text-orange-500" />
            <span className="text-xs font-semibold text-gray-500">Reconexiones</span>
          </div>
          <p className="text-xl font-bold text-gray-800">{reconexiones?.total_actual ?? '—'}</p>
          {reconexiones?.delta_ventana !== null && reconexiones?.delta_ventana !== undefined && (
            <p className={`text-xs ${reconexiones.delta_ventana > 3 ? 'text-red-600' : 'text-gray-400'}`}>
              +{reconexiones.delta_ventana} en {ventana_horas}h
            </p>
          )}
        </div>
      </div>
    </div>
  );
};

export default MetricasTransmision;
