import { useMemo, useState } from 'react';
import {
  AreaChart,
  Area,
  BarChart,
  Bar,
  Cell,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ReferenceLine,
  ResponsiveContainer,
} from 'recharts';
import { VOLTAJE_MIN, VOLTAJE_MAX } from '../../utils/voltage';
import {
  formatBogotaDate,
  formatBogotaTime,
  parseApiTimestamp,
} from '../../utils/dateTime';
import {
  calcularConsumo,
  formatearEnergia,
  intervaloConsumo,
  marcasTiempo,
} from '../../utils/consumo';

const variables = [
  { key: 'potencia', label: 'Potencia', color: '#10b981', unidad: 'W' },
  { key: 'voltaje', label: 'Voltaje', color: '#f59e0b', unidad: 'V' },
  { key: 'corriente', label: 'Corriente', color: '#3b82f6', unidad: 'A' },
  // energia_activa es el contador acumulado del medidor en Wh. La vista de
  // consumo muestra la diferencia del contador por intervalo.
  { key: 'energia', label: 'Consumo', color: '#8b5cf6', unidad: 'Wh' },
];

const COLOR_PARCIAL = '#c4b5fd';
const NOMBRE_INTERVALO = { hora: 'hora', dia: 'día', mes: 'mes' };

const CustomTooltip = ({ active, payload, label }) => {
  if (active && payload && payload.length) {
    return (
      <div className="bg-white p-3 rounded-lg shadow-lg border border-gray-200">
        <p className="text-sm text-gray-600 font-medium">
          {formatBogotaDate(new Date(label), { day: '2-digit', month: '2-digit', year: 'numeric' }, '--')}
          {' '}{formatBogotaTime(new Date(label), '--:--')}
        </p>
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

const etiquetaIntervalo = (inicioMs, intervalo, conFecha) => {
  const fecha = new Date(inicioMs);
  if (intervalo === 'mes') {
    return formatBogotaDate(fecha, { month: 'short', year: 'numeric' }, '--');
  }
  if (intervalo === 'dia') {
    return formatBogotaDate(fecha, { day: '2-digit', month: '2-digit' }, '--/--');
  }
  const hora = formatBogotaTime(fecha, '--:--');
  return conFecha
    ? `${formatBogotaDate(fecha, { day: '2-digit', month: '2-digit' }, '--/--')} ${hora}`
    : hora;
};

const ConsumoTooltip = ({ active, payload, intervalo }) => {
  if (!active || !payload || !payload.length) return null;
  const b = payload[0].payload;
  const desde = new Date(b.inicio);
  const hasta = new Date(b.fin);
  const rango = intervalo === 'hora'
    ? `${formatBogotaDate(desde, { day: '2-digit', month: '2-digit', year: 'numeric' }, '--')} `
      + `${formatBogotaTime(desde, '--:--')}–${formatBogotaTime(hasta, '--:--')}`
    : etiquetaIntervalo(b.inicio, intervalo, true);
  return (
    <div className="bg-white p-3 rounded-lg shadow-lg border border-gray-200 max-w-xs">
      <p className="text-sm text-gray-600 font-medium">{rango}</p>
      <p className="text-sm font-bold text-violet-600">Consumo: {formatearEnergia(b.wh)}</p>
      <p className="text-xs text-gray-500">{b.muestras} lectura{b.muestras === 1 ? '' : 's'} del contador</p>
      {b.parcial && (
        <p className="text-xs text-gray-500">Intervalo parcial: empieza en la primera lectura de la ventana.</p>
      )}
      {b.conHueco && (
        <p className="text-xs text-amber-600">Incluye un periodo anterior sin datos.</p>
      )}
      {b.reinicio && (
        <p className="text-xs text-amber-600">El contador se reinició: solo se cuenta el consumo interno.</p>
      )}
    </div>
  );
};

const Indicador = ({ etiqueta, valor, detalle }) => (
  <div className="rounded-lg bg-violet-50 px-3 py-2">
    <p className="text-xs text-gray-500">{etiqueta}</p>
    <p className="text-base font-semibold text-gray-800">{valor}</p>
    {detalle && <p className="text-[11px] text-gray-400">{detalle}</p>}
  </div>
);

/**
 * Grafico de historico de un dispositivo.
 * Props:
 *   datos = array de mediciones
 *   variableInicial = 'potencia'|'voltaje'|'corriente'|'energia'
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
  periodoInicio,
  periodoFin,
}) => {
  const [variable, setVariable] = useState(variableInicial);

  // En ventanas de más de 48h la hora sola es ambigua (se repite cada día):
  // se antepone día/mes. El umbral se infiere del propio rango de datos.
  const inicioMs = parseApiTimestamp(periodoInicio)?.getTime();
  const finMs = parseApiTimestamp(periodoFin)?.getTime();
  const rangoMs = Number.isFinite(inicioMs) && Number.isFinite(finMs)
    ? Math.abs(finMs - inicioMs)
    : 0;
  const mostrarFecha = rangoMs > 48 * 3600 * 1000;

  const datosFormateados = useMemo(() => datos
    .map((medicion) => {
      const timestampMs = parseApiTimestamp(medicion.timestamp)?.getTime();
      return {
        timestampMs,
        potencia: medicion.potencia_activa,
        voltaje: medicion.voltaje_rms,
        corriente: medicion.corriente_rms,
        energia: medicion.energia_activa,
      };
    })
    .filter((punto) => Number.isFinite(punto.timestampMs))
    .sort((a, b) => a.timestampMs - b.timestampMs), [datos]);

  const intervalo = intervaloConsumo(rangoMs || undefined);
  const consumo = useMemo(
    () => calcularConsumo(datosFormateados, intervalo),
    [datosFormateados, intervalo],
  );

  const dominioTiempo = Number.isFinite(inicioMs) && Number.isFinite(finMs)
    ? [inicioMs, finMs]
    : ['dataMin', 'dataMax'];
  const marcas = Number.isFinite(inicioMs) && Number.isFinite(finMs)
    ? marcasTiempo(inicioMs, finMs)
    : undefined;

  const variableActual = variables.find((v) => v.key === variable) || variables[0];
  const esConsumo = variable === 'energia';

  // Barras en Wh mientras ninguna llegue a 1 kWh; si no, en kWh.
  const maxWh = consumo.barras.reduce((m, b) => Math.max(m, b.wh), 0);
  const enKwh = maxWh >= 1000;
  const barras = consumo.barras.map((b) => ({
    ...b,
    valor: enKwh ? b.wh / 1000 : b.wh,
    etiqueta: etiquetaIntervalo(b.inicio, intervalo, mostrarFecha),
  }));
  const unidadBarras = enKwh ? 'kWh' : 'Wh';
  const r = consumo.resumen;

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

      {esConsumo && r && (
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3 mb-4">
          <Indicador
            etiqueta="Consumo del periodo"
            valor={formatearEnergia(r.totalWh)}
            detalle={`${r.horas.toFixed(1)} h con datos`}
          />
          <Indicador
            etiqueta="Promedio diario"
            valor={r.promedioDiarioWh === null ? '---' : formatearEnergia(r.promedioDiarioWh)}
          />
          <Indicador
            etiqueta="Proyección 30 días"
            valor={r.proyeccion30dWh === null ? '---' : formatearEnergia(r.proyeccion30dWh)}
            detalle="al ritmo del periodo"
          />
          <Indicador
            etiqueta="Lectura del contador"
            valor={formatearEnergia(r.lecturaWh)}
            detalle="acumulado del medidor"
          />
        </div>
      )}

      {datosFormateados.length === 0 || (esConsumo && barras.length === 0) ? (
        <div className="h-64 flex items-center justify-center text-gray-400 text-sm">
          No hay datos historicos disponibles
        </div>
      ) : esConsumo ? (
        <>
          <ResponsiveContainer width="100%" height={300}>
            <BarChart data={barras}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e5e7eb" />
              <XAxis
                dataKey="etiqueta"
                tick={{ fontSize: 12 }}
                stroke="#9ca3af"
                interval="preserveStartEnd"
                minTickGap={12}
                label={{
                  value: intervalo === 'hora' ? 'Hora' : intervalo === 'dia' ? 'Día' : 'Mes',
                  position: 'insideBottomRight',
                  offset: -5,
                  fontSize: 12,
                  fill: '#6b7280',
                }}
              />
              <YAxis
                tick={{ fontSize: 12 }}
                stroke="#9ca3af"
                domain={[0, 'auto']}
                label={{
                  value: `Consumo por ${NOMBRE_INTERVALO[intervalo]} (${unidadBarras})`,
                  angle: -90,
                  position: 'insideLeft',
                  fontSize: 12,
                  fill: '#6b7280',
                }}
              />
              <Tooltip content={<ConsumoTooltip intervalo={intervalo} />} />
              <Bar dataKey="valor" name="Consumo" radius={[3, 3, 0, 0]}>
                {barras.map((b) => (
                  <Cell
                    key={b.inicio}
                    fill={(b.parcial || b.conHueco || b.reinicio) ? COLOR_PARCIAL : variableActual.color}
                  />
                ))}
              </Bar>
            </BarChart>
          </ResponsiveContainer>
          <p className="mt-2 text-xs text-gray-400">
            Barras: diferencia del contador del medidor por {NOMBRE_INTERVALO[intervalo]} (hora de Bogotá).
            Color claro: intervalo parcial, con un periodo previo sin datos o con reinicio del contador.
          </p>
        </>
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
            <XAxis
              dataKey="timestampMs"
              type="number"
              scale="time"
              domain={dominioTiempo}
              ticks={marcas}
              tickFormatter={(value) => {
                const hora = formatBogotaTime(new Date(value), '--:--');
                if (!mostrarFecha) return hora;
                const dia = formatBogotaDate(
                  new Date(value),
                  { day: '2-digit', month: '2-digit' },
                  '--/--',
                );
                return rangoMs > 8 * 24 * 3600 * 1000 ? dia : `${dia} ${hora}`;
              }}
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              label={{ value: 'Hora', position: 'insideBottomRight', offset: -5, fontSize: 12, fill: '#6b7280' }}
            />
            <YAxis
              tick={{ fontSize: 12 }}
              stroke="#9ca3af"
              domain={variable === 'voltaje' ? ['auto', 'auto'] : [0, 'auto']}
              label={{
                value: `${variableActual.label} (${variableActual.unidad})`,
                angle: -90,
                position: 'insideLeft',
                fontSize: 12,
                fill: '#6b7280',
              }}
            />
            <Tooltip content={<CustomTooltip />} />
            <Legend />
            {/* Banda estacionaria CREG para nominal residencial de 120 V. */}
            {variable === 'voltaje' && (
              <>
                <ReferenceLine
                  y={VOLTAJE_MAX}
                  stroke="#ef4444"
                  strokeDasharray="6 4"
                  label={{ value: `Máx CREG ${VOLTAJE_MAX} V`, fontSize: 11, fill: '#ef4444', position: 'insideTopRight' }}
                />
                <ReferenceLine
                  y={VOLTAJE_MIN}
                  stroke="#ef4444"
                  strokeDasharray="6 4"
                  label={{ value: `Mín CREG ${VOLTAJE_MIN} V`, fontSize: 11, fill: '#ef4444', position: 'insideBottomRight' }}
                />
              </>
            )}
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
