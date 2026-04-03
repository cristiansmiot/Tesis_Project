import { Zap, Activity, Gauge, TrendingUp, Clock, Battery } from 'lucide-react';
import KpiCard from '../common/KpiCard';

/**
 * Fila de KPIs para el detalle de un medidor.
 * Props: medicion = última medición del dispositivo
 */
const DeviceKpiCards = ({ medicion }) => {
  const voltaje = medicion?.voltaje_rms?.toFixed(1) || '---';
  const corriente = medicion?.corriente_rms?.toFixed(2) || '---';
  const potencia = medicion?.potencia_activa?.toFixed(1) || '---';
  const fp = medicion?.factor_potencia?.toFixed(2) || '---';
  const energia = medicion?.energia_activa?.toFixed(1) || '---';
  const ultimaLectura = medicion?.timestamp
    ? new Date(medicion.timestamp).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' })
    : '---';

  return (
    <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-6 gap-4">
      <KpiCard label="Energía" valor={energia} unidad="kWh" icon={Battery} />
      <KpiCard label="Tensión" valor={voltaje} unidad="V" icon={Zap} />
      <KpiCard label="Corriente" valor={corriente} unidad="A" icon={Activity} />
      <KpiCard label="Potencia activa" valor={potencia} unidad="W" icon={Gauge} />
      <KpiCard label="Factor de potencia" valor={fp} icon={TrendingUp} />
      <KpiCard label="Última lectura" valor={ultimaLectura} icon={Clock} />
    </div>
  );
};

export default DeviceKpiCards;
