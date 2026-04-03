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
const DeviceVariablesPanel = ({ medicion, saludData }) => {
  const voltajeStatus = evaluarVoltaje(medicion?.voltaje_rms);
  const fpStatus = evaluarFactorPotencia(medicion?.factor_potencia);
  const freqStatus = evaluarFrecuencia(medicion?.frecuencia);

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
          <div className="grid grid-cols-2 md:grid-cols-3 gap-4">
            <StatusItem label="Linea AC" activo={saludData.ac_ok} />
            <StatusItem label="Carga detectada" activo={saludData.carga} />
            <StatusItem label="ADE9153A" activo={saludData.ade_ok} descripcion="Sensor de energia" />
            <StatusItem label="Bus SPI (ADE)" activo={saludData.ade_bus} />
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
          </div>
        ) : (
          <p className="text-sm text-gray-400">
            Sin datos de estado. El nodo aun no ha enviado un mensaje de /estado.
          </p>
        )}
      </div>

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
