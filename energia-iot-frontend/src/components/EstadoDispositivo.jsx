import { Wifi, WifiOff, Clock, MapPin, Cpu, Signal } from 'lucide-react';

/**
 * Indicador binario ON/OFF con etiqueta y color condicional.
 */
const Indicador = ({ label, valor }) => {
  const base = 'flex flex-col items-center gap-1 p-2 rounded-lg text-center';
  if (valor === null || valor === undefined) {
    return (
      <div className={`${base} bg-gray-100`}>
        <span className="w-3 h-3 rounded-full bg-gray-300" />
        <span className="text-xs text-gray-400">{label}</span>
      </div>
    );
  }
  return (
    <div className={`${base} ${valor ? 'bg-green-50' : 'bg-red-50'}`}>
      <span className={`w-3 h-3 rounded-full ${valor ? 'bg-green-500' : 'bg-red-500'}`} />
      <span className={`text-xs font-medium ${valor ? 'text-green-700' : 'text-red-600'}`}>
        {label}
      </span>
    </div>
  );
};

/**
 * Componente que muestra el estado del dispositivo ESP32 y la salud del nodo.
 * Props:
 *   dispositivo  – objeto del dispositivo (nombre, device_id, ubicacion…)
 *   estado       – objeto del endpoint /estado (conectado, ultima_conexion…)
 *   salud        – objeto del endpoint /salud (ac_ok, ade_ok, rssi_dbm…)
 */
const EstadoDispositivo = ({ dispositivo, estado, salud }) => {
  const conectado = estado?.conectado || false;

  const formatearTiempo = (timestamp) => {
    if (!timestamp) return 'Nunca';
    return new Date(timestamp).toLocaleString('es-CO', {
      day: '2-digit', month: '2-digit', year: 'numeric',
      hour: '2-digit', minute: '2-digit',
    });
  };

  const rssiLabel = (dbm) => {
    if (dbm === null || dbm === undefined || dbm <= -127) return 'N/A';
    if (dbm >= -70) return `${dbm} dBm ✓`;
    if (dbm >= -85) return `${dbm} dBm`;
    return `${dbm} dBm ⚠`;
  };

  return (
    <div className="bg-white rounded-xl shadow-md p-6 space-y-5">
      {/* ── Cabecera ─────────────────────────────────────────────────── */}
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold text-gray-700">🔌 Estado del Dispositivo</h3>
        <div className={`flex items-center gap-2 px-3 py-1 rounded-full text-sm font-medium ${
          conectado ? 'bg-green-100 text-green-700' : 'bg-red-100 text-red-700'
        }`}>
          <div className={conectado ? 'estado-conectado' : 'estado-desconectado'} />
          {conectado ? 'Conectado' : 'Desconectado'}
        </div>
      </div>

      {/* ── Info del dispositivo ────────────────────────────────────── */}
      <div className="space-y-3">
        <div className="flex items-center gap-3">
          <div className="p-2 bg-blue-100 rounded-lg">
            <Cpu className="w-5 h-5 text-blue-600" />
          </div>
          <div>
            <p className="text-xs text-gray-500">Dispositivo</p>
            <p className="font-medium text-gray-800">
              {dispositivo?.nombre || estado?.nombre || 'Sin nombre'}
            </p>
          </div>
        </div>

        <div className="flex items-center gap-3">
          <div className="p-2 bg-purple-100 rounded-lg">
            {conectado
              ? <Wifi className="w-5 h-5 text-purple-600" />
              : <WifiOff className="w-5 h-5 text-purple-600" />}
          </div>
          <div>
            <p className="text-xs text-gray-500">Device ID</p>
            <p className="font-mono font-medium text-gray-800">
              {dispositivo?.device_id || estado?.device_id || 'N/A'}
            </p>
          </div>
        </div>

        {dispositivo?.ubicacion && (
          <div className="flex items-center gap-3">
            <div className="p-2 bg-green-100 rounded-lg">
              <MapPin className="w-5 h-5 text-green-600" />
            </div>
            <div>
              <p className="text-xs text-gray-500">Ubicación</p>
              <p className="font-medium text-gray-800">{dispositivo.ubicacion}</p>
            </div>
          </div>
        )}

        <div className="flex items-center gap-3">
          <div className="p-2 bg-orange-100 rounded-lg">
            <Clock className="w-5 h-5 text-orange-600" />
          </div>
          <div>
            <p className="text-xs text-gray-500">Última conexión</p>
            <p className="font-medium text-gray-800">
              {formatearTiempo(estado?.ultima_conexion || dispositivo?.ultima_conexion)}
            </p>
            {estado?.tiempo_sin_conexion && (
              <p className="text-xs text-gray-400">Hace {estado.tiempo_sin_conexion}</p>
            )}
          </div>
        </div>
      </div>

      {/* ── Indicadores de salud (topic /estado) ────────────────────── */}
      {salud && (
        <>
          <div>
            <p className="text-xs font-semibold text-gray-500 uppercase tracking-wide mb-2">
              Salud del Nodo
            </p>
            <div className="grid grid-cols-3 gap-1">
              <Indicador label="AC"    valor={salud.ac_ok}    />
              <Indicador label="Carga" valor={salud.carga}    />
              <Indicador label="ADE"   valor={salud.ade_ok}   />
              <Indicador label="Módem" valor={salud.modem_ok} />
              <Indicador label="MQTT"  valor={salud.mqtt_ok}  />
              <Indicador label="Cal."  valor={salud.cal_ok}   />
            </div>
          </div>

          {/* ── Métricas de transmisión ──────────────────────────────── */}
          <div>
            <p className="text-xs font-semibold text-gray-500 uppercase tracking-wide mb-2">
              Transmisión
            </p>
            <div className="grid grid-cols-2 gap-2 text-sm">
              <div className="bg-gray-50 rounded-lg p-2 flex items-center gap-2">
                <Signal className="w-4 h-4 text-indigo-500 shrink-0" />
                <div>
                  <p className="text-xs text-gray-400">RSSI</p>
                  <p className="font-medium text-gray-700">{rssiLabel(salud.rssi_dbm)}</p>
                </div>
              </div>
              <div className="bg-gray-50 rounded-lg p-2">
                <p className="text-xs text-gray-400">Mensajes TX</p>
                <p className="font-medium text-gray-700">{salud.msg_tx ?? '—'}</p>
              </div>
              <div className="bg-gray-50 rounded-lg p-2">
                <p className="text-xs text-gray-400">Reconexiones</p>
                <p className="font-medium text-gray-700">{salud.reconexiones ?? '—'}</p>
              </div>
              {salud.ade_rec !== null && salud.ade_rec !== undefined && (
                <div className="bg-gray-50 rounded-lg p-2">
                  <p className="text-xs text-gray-400">ADE rec.</p>
                  <p className="font-medium text-gray-700">{salud.ade_rec}</p>
                </div>
              )}
            </div>
          </div>

          {salud.fw_version && (
            <p className="text-xs text-gray-400 text-right">FW {salud.fw_version}</p>
          )}
        </>
      )}
    </div>
  );
};

export default EstadoDispositivo;
