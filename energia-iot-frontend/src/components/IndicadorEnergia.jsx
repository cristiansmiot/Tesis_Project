import { Zap, Gauge, Activity, TrendingUp } from 'lucide-react';

/**
 * Componente que muestra un indicador de energía (voltaje, corriente, potencia, etc.)
 */
const iconos = {
  voltaje: Zap,
  corriente: Activity,
  potencia: Gauge,
  factor: TrendingUp,
  energia: Zap,
};

const colores = {
  voltaje: 'from-yellow-400 to-orange-500',
  corriente: 'from-blue-400 to-blue-600',
  potencia: 'from-green-400 to-emerald-600',
  factor: 'from-purple-400 to-purple-600',
  energia: 'from-red-400 to-red-600',
};

const IndicadorEnergia = ({ tipo, valor, unidad, label, descripcion }) => {
  const Icono = iconos[tipo] || Zap;
  const gradiente = colores[tipo] || 'from-gray-400 to-gray-600';

  return (
    <div className="bg-white rounded-xl shadow-md p-6 card-hover">
      <div className="flex items-center justify-between mb-4">
        <div className={`p-3 rounded-lg bg-gradient-to-br ${gradiente}`}>
          <Icono className="w-6 h-6 text-white" />
        </div>
        <span className="text-sm text-gray-500 font-medium">{label}</span>
      </div>
      
      <div className="mt-2">
        <div className="flex items-baseline gap-2">
          <span className="valor-grande text-gray-800">{valor}</span>
          <span className="text-xl text-gray-500 font-medium">{unidad}</span>
        </div>
        {descripcion && (
          <p className="text-sm text-gray-400 mt-2">{descripcion}</p>
        )}
      </div>
    </div>
  );
};

export default IndicadorEnergia;
