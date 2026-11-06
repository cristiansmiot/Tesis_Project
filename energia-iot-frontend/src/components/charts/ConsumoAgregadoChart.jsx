import {
  BarChart,
  Bar,
  Cell,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from 'recharts';
import { formatearEnergia } from '../../utils/consumo';

const COLOR_NORMAL = '#3b82f6';
const COLOR_HUECO = '#93c5fd';

const siguienteHora = (hora) => {
  const h = Number.parseInt(hora, 10);
  return Number.isFinite(h) ? `${String((h + 1) % 24).padStart(2, '0')}:00` : '';
};

const CustomTooltip = ({ active, payload }) => {
  if (!active || !payload || !payload.length) return null;
  const d = payload[0].payload;
  return (
    <div className="bg-white p-3 rounded-lg shadow-lg border border-gray-200">
      <p className="text-sm text-gray-600 font-medium">
        {d.hora}–{siguienteHora(d.hora)} (hora local)
      </p>
      {d.muestras > 0 ? (
        <>
          <p className="text-sm font-bold text-blue-600">{formatearEnergia(d.wh)}</p>
          <p className="text-xs text-gray-500">{d.muestras} lecturas</p>
          {d.con_hueco && (
            <p className="text-xs text-amber-600">Incluye consumo de un periodo sin datos</p>
          )}
        </>
      ) : (
        <p className="text-sm text-gray-500">Sin lecturas en esta hora</p>
      )}
    </div>
  );
};

/**
 * Consumo agregado de todos los medidores por hora local de Bogota, ultimas 24 h.
 * Props: datos = [{ hora: '14:00', consumo: kWh, wh, muestras, con_hueco }, ...]
 * Cada barra es la diferencia del contador de energia (calculada en el backend).
 */
const ConsumoAgregadoChart = ({ datos = [] }) => {
  const filas = datos.map((d) => ({
    ...d,
    wh: Number.isFinite(d.wh) ? d.wh : (d.consumo ?? 0) * 1000,
    muestras: d.muestras ?? 0,
  }));
  const hayLecturas = filas.some((d) => d.muestras > 0 || d.wh > 0);
  // Wh mientras el consumo horario sea pequeno; kWh desde 1 kWh por hora.
  const enKwh = filas.some((d) => d.wh >= 1000);
  const clave = enKwh ? 'consumo' : 'wh';

  return (
    <div className="bg-white rounded-xl border border-gray-200 p-6">
      <h3 className="text-lg font-semibold text-gray-800">Consumo por hora</h3>
      <p className="text-xs text-gray-500 mb-4">Últimas 24 h · hora local de Bogotá · todos los medidores</p>

      {!hayLecturas ? (
        <div className="h-64 flex items-center justify-center text-gray-400 text-sm">
          No hay lecturas de energía en las últimas 24 h
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={300}>
          <BarChart data={filas} barCategoryGap="20%">
            <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" vertical={false} />
            <XAxis
              dataKey="hora"
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              tickLine={false}
              interval={1}
              label={{ value: 'Hora local', position: 'insideBottomRight', offset: -5, fontSize: 12, fill: '#6b7280' }}
            />
            <YAxis
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              tickLine={false}
              axisLine={false}
              label={{ value: enKwh ? 'kWh' : 'Wh', angle: -90, position: 'insideLeft', fontSize: 12, fill: '#6b7280' }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Bar dataKey={clave} radius={[4, 4, 0, 0]} maxBarSize={40}>
              {filas.map((d) => (
                <Cell key={d.inicio ?? d.hora} fill={d.con_hueco ? COLOR_HUECO : COLOR_NORMAL} />
              ))}
            </Bar>
          </BarChart>
        </ResponsiveContainer>
      )}
    </div>
  );
};

export default ConsumoAgregadoChart;
