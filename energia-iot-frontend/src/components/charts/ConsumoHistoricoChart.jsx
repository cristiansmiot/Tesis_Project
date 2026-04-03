import { useState } from 'react';
import {
  AreaChart,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
} from 'recharts';

const variables = [
  { key: 'potencia', label: 'Potencia', color: '#10b981', unidad: 'W' },
  { key: 'voltaje', label: 'Voltaje', color: '#f59e0b', unidad: 'V' },
  { key: 'corriente', label: 'Corriente', color: '#3b82f6', unidad: 'A' },
];

const CustomTooltip = ({ active, payload, label }) => {
  if (active && payload && payload.length) {
    return (
      <div className="bg-white p-3 rounded-lg shadow-lg border border-gray-200">
        <p className="text-sm text-gray-600 font-medium">{label}</p>
        {payload.map((entry, index) => {
          const variable = variables.find((v) => v.key === entry.dataKey);
          return (
            <p key={index} style={{ color: entry.color }} className="text-sm font-bold">
              {entry.name}: {entry.value?.toFixed(2)} {variable?.unidad || ''}
            </p>
          );
        })}
      </div>
    );
  }
  return null;
};

/**
 * Grafico de historico de consumo de un dispositivo.
 * Props:
 *   datos = array de mediciones
 *   variableInicial = 'potencia'|'voltaje'|'corriente'
 *   titulo = titulo personalizado (default: 'Historico')
 *   periodos = array opcional de {h, label} para selector de periodo
 *   periodoActual = periodo seleccionado
 *   onCambioPeriodo = callback al cambiar periodo
 */
const ConsumoHistoricoChart = ({
  datos = [],
  variableInicial = 'potencia',
  titulo = 'Historico',
  periodos,
  periodoActual,
  onCambioPeriodo,
}) => {
  const [variable, setVariable] = useState(variableInicial);

  const datosFormateados = datos
    .map((medicion) => {
      const fecha = new Date(medicion.timestamp);
      return {
        hora: fecha.toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' }),
        potencia: medicion.potencia_activa,
        voltaje: medicion.voltaje_rms,
        corriente: medicion.corriente_rms,
      };
    })
    .reverse();

  const variableActual = variables.find((v) => v.key === variable) || variables[0];

  return (
    <div className="bg-white rounded-xl border border-gray-200 p-6">
      <div className="flex items-center justify-between mb-4">
        <h3 className="text-lg font-semibold text-gray-800">{titulo}</h3>
        <div className="flex gap-1">
          {/* Selector de periodo (si se proporciona) */}
          {periodos && periodos.map(({ h, label }) => (
            <button
              key={h}
              onClick={() => onCambioPeriodo?.(h)}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-colors ${
                periodoActual === h
                  ? 'bg-blue-100 text-blue-700'
                  : 'bg-gray-100 text-gray-500 hover:bg-gray-200'
              }`}
            >
              {label}
            </button>
          ))}
          {/* Separador si hay ambos */}
          {periodos && <div className="w-px bg-gray-200 mx-1" />}
          {/* Selector de variable */}
          {variables.map((v) => (
            <button
              key={v.key}
              onClick={() => setVariable(v.key)}
              className={`px-3 py-1 rounded-lg text-xs font-medium transition-colors ${
                variable === v.key
                  ? 'bg-blue-100 text-blue-700'
                  : 'bg-gray-100 text-gray-500 hover:bg-gray-200'
              }`}
            >
              {v.label}
            </button>
          ))}
        </div>
      </div>

      {datosFormateados.length === 0 ? (
        <div className="h-64 flex items-center justify-center text-gray-400 text-sm">
          No hay datos historicos disponibles
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={300}>
          <AreaChart data={datosFormateados}>
            <defs>
              <linearGradient id={`color-${variable}`} x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor={variableActual.color} stopOpacity={0.3} />
                <stop offset="95%" stopColor={variableActual.color} stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
            <XAxis dataKey="hora" tick={{ fontSize: 12 }} stroke="#9ca3af" />
            <YAxis
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              label={{
                value: variableActual.unidad,
                angle: -90,
                position: 'insideLeft',
                fontSize: 12,
                fill: '#6b7280',
              }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Legend />
            <Area
              type="monotone"
              dataKey={variable}
              name={variableActual.label}
              stroke={variableActual.color}
              fillOpacity={1}
              fill={`url(#color-${variable})`}
              strokeWidth={2}
            />
          </AreaChart>
        </ResponsiveContainer>
      )}
    </div>
  );
};

export default ConsumoHistoricoChart;
