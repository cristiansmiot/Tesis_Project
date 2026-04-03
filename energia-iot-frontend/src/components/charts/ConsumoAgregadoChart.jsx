import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from 'recharts';

const CustomTooltip = ({ active, payload, label }) => {
  if (active && payload && payload.length) {
    return (
      <div className="bg-white p-3 rounded-lg shadow-lg border border-gray-200">
        <p className="text-sm text-gray-600 font-medium">{label}</p>
        <p className="text-sm font-bold text-blue-600">
          {payload[0].value?.toFixed(2)} kWh
        </p>
      </div>
    );
  }
  return null;
};

/**
 * Gráfico de barras de consumo agregado por hora.
 * Props: datos = [{ hora: '00:00', consumo: 12.5 }, ...]
 */
const ConsumoAgregadoChart = ({ datos = [] }) => {
  return (
    <div className="bg-white rounded-xl border border-gray-200 p-6">
      <h3 className="text-lg font-semibold text-gray-800 mb-4">Consumo agregado</h3>

      {datos.length === 0 ? (
        <div className="h-64 flex items-center justify-center text-gray-400 text-sm">
          No hay datos de consumo disponibles
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={300}>
          <BarChart data={datos} barCategoryGap="20%">
            <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" vertical={false} />
            <XAxis
              dataKey="hora"
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              tickLine={false}
            />
            <YAxis
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              tickLine={false}
              axisLine={false}
              label={{ value: 'kWh', angle: -90, position: 'insideLeft', fontSize: 12, fill: '#6b7280' }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Bar
              dataKey="consumo"
              fill="#3b82f6"
              radius={[4, 4, 0, 0]}
              maxBarSize={40}
            />
          </BarChart>
        </ResponsiveContainer>
      )}
    </div>
  );
};

export default ConsumoAgregadoChart;
