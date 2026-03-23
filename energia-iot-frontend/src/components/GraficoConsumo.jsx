import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  Area,
  AreaChart,
} from 'recharts';

/**
 * Componente que muestra el gráfico de consumo de energía
 */
const GraficoConsumo = ({ datos, tipo = 'potencia' }) => {
  // Formatear datos para el gráfico
  const datosFormateados = datos.map((medicion, index) => {
    const fecha = new Date(medicion.timestamp);
    return {
      hora: fecha.toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' }),
      potencia: medicion.potencia_activa,
      voltaje: medicion.voltaje_rms,
      corriente: medicion.corriente_rms,
      index: index,
    };
  }).reverse(); // Ordenar de más antiguo a más reciente

  const colores = {
    potencia: '#10b981',
    voltaje: '#f59e0b',
    corriente: '#3b82f6',
  };

  const unidades = {
    potencia: 'W',
    voltaje: 'V',
    corriente: 'A',
  };

  const CustomTooltip = ({ active, payload, label }) => {
    if (active && payload && payload.length) {
      return (
        <div className="bg-white p-3 rounded-lg shadow-lg border border-gray-200">
          <p className="text-sm text-gray-600 font-medium">{label}</p>
          {payload.map((entry, index) => (
            <p key={index} style={{ color: entry.color }} className="text-sm font-bold">
              {entry.name}: {entry.value?.toFixed(2)} {unidades[entry.dataKey]}
            </p>
          ))}
        </div>
      );
    }
    return null;
  };

  return (
    <div className="bg-white rounded-xl shadow-md p-6">
      <h3 className="text-lg font-semibold text-gray-700 mb-4">
        📊 Histórico de Consumo
      </h3>
      
      {datosFormateados.length === 0 ? (
        <div className="h-64 flex items-center justify-center text-gray-400">
          No hay datos para mostrar
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={300}>
          <AreaChart data={datosFormateados}>
            <defs>
              <linearGradient id="colorPotencia" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#10b981" stopOpacity={0.3}/>
                <stop offset="95%" stopColor="#10b981" stopOpacity={0}/>
              </linearGradient>
              <linearGradient id="colorVoltaje" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#f59e0b" stopOpacity={0.3}/>
                <stop offset="95%" stopColor="#f59e0b" stopOpacity={0}/>
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
            <XAxis 
              dataKey="hora" 
              tick={{ fontSize: 12 }} 
              stroke="#9ca3af"
            />
            <YAxis 
              tick={{ fontSize: 12 }} 
              stroke="#9ca3af"
              label={{ value: 'Watts', angle: -90, position: 'insideLeft', fontSize: 12 }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Legend />
            <Area
              type="monotone"
              dataKey="potencia"
              name="Potencia"
              stroke="#10b981"
              fillOpacity={1}
              fill="url(#colorPotencia)"
              strokeWidth={2}
            />
          </AreaChart>
        </ResponsiveContainer>
      )}
    </div>
  );
};

export default GraficoConsumo;
