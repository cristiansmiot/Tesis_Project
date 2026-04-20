import { Zap, Activity, Gauge, TrendingUp, Thermometer, Radio, Cpu, Battery, BatteryCharging } from 'lucide-react';
import StatusBadge from '../common/StatusBadge';
import { evaluarVoltaje, evaluarFactorPotencia, evaluarFrecuencia } from '../../utils/voltage';

/**
 * Panel ampliado de variables del medidor.
 * Muestra metricas de energia detalladas + estado del nodo + sensores,
 * SIN repetir los indicadores basicos que ya estan en Resumen.
 *
 * Props: medicion = ultima medicion, saludData = ultimo estado del nodo
 */
const DeviceVariablesPanel = ({ medicion, saludData, reconciliacion }) => {
  // ac_ok puede ser true/false/null; null = aún no se conoce → no restringir alertas
  const acPresente = saludData?.ac_ok ?? null;
  const voltajeStatus = evaluarVoltaje(medicion?.voltaje_rms, acPresente);
  const fpStatus = evaluarFactorPotencia(medicion?.factor_potencia, acPresente);
  const freqStatus = evaluarFrecuencia(medicion?.frecuencia, acPresente);

  return (
    <div className="space-y-6">
      {/* Metricas de Energia */}
      <div className="bg-white rounded-xl border border-gray-200 p-6">
        <h4 className="text-base font-semibold text-gray-800 mb-4 flex items-center gap-2">
          <Zap className="w-5 h-5 text-yellow-500" />
          Metricas de energia
        </h4>
        <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
          <VariableCard
            label="Voltaje RMS"
            valor={medicion?.voltaje_rms?.toFixed(2)}
            unidad="V"
            status={voltajeStatus}
          />
          <VariableCard
            label="Corriente RMS"
            valor={medicion?.corriente_rms?.toFixed(4)}
            unidad="A"
          />
          <VariableCard
            label="Potencia activa"
            valor={medicion?.potencia_activa?.toFixed(2)}
            unidad="W"
          />
          <VariableCard
            label="Potencia reactiva"
            valor={medicion?.potencia_reactiva?.toFixed(2)}
            unidad="VAR"
          />
          <VariableCard
            label="Potencia aparente"
            valor={medicion?.potencia_aparente?.toFixed(2)}
            unidad="VA"
          />
          <VariableCard
            label="Factor de potencia"
            valor={medicion?.factor_potencia?.toFixed(3)}
            unidad=""
            status={fpStatus}
          />
          <VariableCard
            label="Frecuencia"
            valor={medicion?.frecuencia?.toFixed(2)}
            unidad="Hz"
            status={freqStatus}
          />
          <VariableCard
            label="Energia activa"
            valor={medicion?.energia_activa?.toFixed(4)}
            unidad="Wh"
          />
          <VariableCard
            label="Energia reactiva"
            valor={medicion?.energia_reactiva?.toFixed(4)}
            unidad="VARh"
          />
        </div>
      </div>

      {/* Estado del Nodo ESP32 */}
      <div className="bg-white rounded-xl border border-gray-200 p-6">
        <h4 className="text-base font-semibold text-gray-800 mb-4 flex items-center gap-2">
          <Cpu className="w-5 h-5 text-blue-500" />
          Estado del nodo ESP32
        </h4>
        {saludData ? (
          <>
            {saludData.ac_ok === false && (
              <div className="mb-3 flex items-center gap-2 rounded-lg bg-amber-50 border border-amber-200 px-3 py-2 text-sm text-amber-800">
                <span className="font-semibold">⚠ Sin línea AC</span>
                <span className="text-amber-600">— las métricas eléctricas no son aplicables.</span>
              </div>
            )}
            <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
              <StatusItem label="Linea AC" activo={saludData.ac_ok} />
              <StatusItem label="Carga detectada" activo={saludData.carga} />
              <StatusItem label="ADE9153A" activo={saludData.ade_ok} descripcion="Sensor de energia" />
              {/* ade_bus almacena bus_disconnected: false=OK, true=Error → invertir */}
              <StatusItem
                label="Bus SPI (ADE)"
                activo={saludData.ade_bus != null ? !saludData.ade_bus : null}
              />
              <StatusItem label="Modem SIM7080G" activo={saludData.modem_ok} descripcion="Celular" />
              <StatusItem label="Sesion MQTT" activo={saludData.mqtt_ok} />
              <StatusItem label="Calibracion mSure" activo={saludData.cal_ok} />
              <div className="border border-gray-100 rounded-lg p-3">
                <p className="text-xs text-gray-500">Recuperaciones ADE</p>
                <p className="text-lg font-bold text-gray-800">{saludData.ade_rec ?? 'N/A'}</p>
              </div>
              <div className="border border-gray-100 rounded-lg p-3">
                <p className="text-xs text-gray-500">Firmware</p>
                <p className="text-lg font-bold text-gray-800">{saludData.fw_version ?? 'N/A'}</p>
              </div>
              {saludData.imei && (
                <div className="border border-gray-100 rounded-lg p-3 col-span-2 md:col-span-3">
                  <p className="text-xs text-gray-500">IMEI (SIM7080G)</p>
                  <p className="text-sm font-mono font-bold text-gray-800">{saludData.imei}</p>
                </div>
              )}
            </div>
          </>
        ) : (
          <p className="text-sm text-gray-400">
            Sin datos de estado. El nodo aun no ha enviado un mensaje de /estado.
          </p>
        )}
      </div>

      {/* Reconciliación de energía */}
      {reconciliacion && (
        <div className="bg-white rounded-xl border border-gray-200 p-6">
          <h4 className="text-base font-semibold text-gray-800 mb-4 flex items-center gap-2">
            <TrendingUp className="w-5 h-5 text-purple-500" />
            Reconciliacion de energia
          </h4>
          {reconciliacion.reset_detectado ? (
            <p className="text-sm text-amber-600">⚠ Reset de firmware detectado — referencia no disponible</p>
          ) : (
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
              <div className="border border-gray-100 rounded-lg p-3">
                <p className="text-xs text-gray-500">Dispositivo (NVS)</p>
                <p className="text-xl font-bold text-gray-900">
                  {reconciliacion.energia_dispositivo_wh?.toFixed(3) ?? '—'}
                </p>
                <p className="text-xs text-gray-400">Wh</p>
              </div>
              <div className="border border-gray-100 rounded-lg p-3">
                <p className="text-xs text-gray-500">Plataforma (∑P·Δt)</p>
                <p className="text-xl font-bold text-gray-900">
                  {reconciliacion.energia_plataforma_wh?.toFixed(3) ?? '—'}
                </p>
                <p className="text-xs text-gray-400">Wh</p>
              </div>
              <div className="border border-gray-100 rounded-lg p-3">
                <p className="text-xs text-gray-500">Discrepancia</p>
                <p className={`text-xl font-bold ${
                  (reconciliacion.discrepancia_pct ?? 0) > 5 ? 'text-red-600' :
                  (reconciliacion.discrepancia_pct ?? 0) > 2 ? 'text-yellow-600' : 'text-green-600'
                }`}>
                  {reconciliacion.discrepancia_pct != null ? `${reconciliacion.discrepancia_pct}%` : '—'}
                </p>
                <p className="text-xs text-gray-400">
                  {reconciliacion.discrepancia_wh != null
                    ? `${reconciliacion.discrepancia_wh > 0 ? '+' : ''}${reconciliacion.discrepancia_wh?.toFixed(3)} Wh`
                    : ''}
                </p>
              </div>
              <div className="border border-gray-100 rounded-lg p-3">
                <p className="text-xs text-gray-500">Mediciones</p>
                <p className="text-xl font-bold text-gray-900">{reconciliacion.total_mediciones}</p>
                <p className="text-xs text-gray-400">en {reconciliacion.ventana_horas}h</p>
              </div>
            </div>
          )}
        </div>
      )}

      {/* Info de ultima medicion */}
      <div className="bg-white rounded-xl border border-gray-200 p-6">
        <h4 className="text-base font-semibold text-gray-800 mb-4 flex items-center gap-2">
          <Activity className="w-5 h-5 text-green-500" />
          Informacion de la medicion
        </h4>
        <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
          <div className="border border-gray-100 rounded-lg p-3">
            <p className="text-xs text-gray-500">Timestamp</p>
            <p className="text-sm font-medium text-gray-800">
              {medicion?.timestamp ? new Date(medicion.timestamp).toLocaleString('es-CO') : 'N/A'}
            </p>
          </div>
          <div className="border border-gray-100 rounded-lg p-3">
            <p className="text-xs text-gray-500">ID medicion</p>
            <p className="text-sm font-mono font-medium text-gray-800">{medicion?.id ?? 'N/A'}</p>
          </div>
          <div className="border border-gray-100 rounded-lg p-3">
            <p className="text-xs text-gray-500">THD voltaje</p>
            <p className="text-sm font-medium text-gray-800">
              {medicion?.thd_voltaje != null ? `${medicion.thd_voltaje.toFixed(2)}%` : 'N/A'}
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};

/**
 * Tarjeta individual de variable con valor y unidad.
 */
const VariableCard = ({ label, valor, unidad, status }) => (
  <div className="border border-gray-100 rounded-lg p-3">
    <div className="flex items-center justify-between mb-1">
      <p className="text-xs text-gray-500">{label}</p>
      {status && <StatusBadge label={status.label} color={status.color} showDot={false} />}
    </div>
    <div className="flex items-baseline gap-1">
      <span className="text-xl font-bold text-gray-900">{valor ?? '---'}</span>
      {unidad && <span className="text-xs text-gray-500">{unidad}</span>}
    </div>
  </div>
);

/**
 * Item de estado booleano del nodo (OK / Error / N/A).
 */
const StatusItem = ({ label, activo, descripcion }) => (
  <div className="border border-gray-100 rounded-lg p-3">
    <p className="text-xs text-gray-500">{label}</p>
    {descripcion && <p className="text-[10px] text-gray-400">{descripcion}</p>}
    {activo == null ? (
      <span className="text-sm font-medium text-gray-400">N/A</span>
    ) : activo ? (
      <span className="text-sm font-bold text-green-600">OK</span>
    ) : (
      <span className="text-sm font-bold text-red-600">Error</span>
    )}
  </div>
);

export default DeviceVariablesPanel;
