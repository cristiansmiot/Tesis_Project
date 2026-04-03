import StatusBadge from '../common/StatusBadge';
import { evaluarVoltaje, evaluarFactorPotencia, evaluarFrecuencia } from '../../utils/voltage';

/**
 * Grid de indicadores actuales con badges de estado.
 * Props: medicion = última medición
 */
const DeviceIndicators = ({ medicion }) => {
  const voltajeStatus = evaluarVoltaje(medicion?.voltaje_rms);
  const fpStatus = evaluarFactorPotencia(medicion?.factor_potencia);
  const freqStatus = evaluarFrecuencia(medicion?.frecuencia);

  const indicadores = [
    {
      label: 'Energía consumida',
      valor: medicion?.energia_activa?.toFixed(1) || '---',
      unidad: 'kWh',
      status: { color: 'green', label: 'Normal' },
    },
    {
      label: 'Voltaje RMS',
      valor: medicion?.voltaje_rms?.toFixed(1) || '---',
      unidad: 'V',
      status: voltajeStatus,
    },
    {
      label: 'Corriente RMS',
      valor: medicion?.corriente_rms?.toFixed(2) || '---',
      unidad: 'A',
      status: { color: 'green', label: 'Normal' },
    },
    {
      label: 'Potencia activa',
      valor: medicion?.potencia_activa?.toFixed(1) || '---',
      unidad: 'W',
      status: { color: 'green', label: 'Normal' },
    },
    {
      label: 'Factor de potencia',
      valor: medicion?.factor_potencia?.toFixed(2) || '---',
      unidad: '',
      status: fpStatus,
    },
    {
      label: 'Frecuencia',
      valor: medicion?.frecuencia?.toFixed(1) || '---',
      unidad: 'Hz',
      status: freqStatus,
    },
  ];

  return (
    <div className="bg-white rounded-xl border border-gray-200 p-6">
      <h4 className="text-base font-semibold text-gray-800 mb-4">Indicadores actuales</h4>
      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        {indicadores.map(({ label, valor, unidad, status }) => (
          <div key={label} className="border border-gray-100 rounded-lg p-4">
            <div className="flex items-center justify-between mb-2">
              <span className="text-sm text-gray-500">{label}</span>
              <StatusBadge label={status.label} color={status.color} showDot={false} />
            </div>
            <div className="flex items-baseline gap-1">
              <span className="text-2xl font-bold text-gray-900">{valor}</span>
              {unidad && <span className="text-sm text-gray-500">{unidad}</span>}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};

export default DeviceIndicators;
