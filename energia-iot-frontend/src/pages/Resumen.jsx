import { useState, useEffect } from 'react';
import { useOutletContext } from 'react-router-dom';
import { Monitor, Wifi, WifiOff, AlertTriangle, Zap } from 'lucide-react';
import KpiCard from '../components/common/KpiCard';
import ConsumoAgregadoChart from '../components/charts/ConsumoAgregadoChart';
import ActiveAlertsList from '../components/alerts/ActiveAlertsList';
import { dashboardAPI, eventosAPI } from '../services/api';

const Resumen = () => {
  const { refreshKey } = useOutletContext();
  const [metricas, setMetricas] = useState({ total_dispositivos: 0, online: 0, offline: 0, alarmas_activas: 0, consumo_total_kwh: 0 });
  const [consumoHorario, setConsumoHorario] = useState([]);
  const [alertas, setAlertas] = useState([]);
  const [cargando, setCargando] = useState(true);

  useEffect(() => {
    cargarDatos();
  }, [refreshKey]);

  const cargarDatos = async () => {
    setCargando(true);
    try {
      const [resumen, consumo, eventosActivos] = await Promise.allSettled([
        dashboardAPI.resumen(),
        dashboardAPI.consumoHorario(24),
        eventosAPI.activos(10),
      ]);

      if (resumen.status === 'fulfilled') {
        setMetricas(resumen.value);
      }

      if (consumo.status === 'fulfilled') {
        setConsumoHorario(consumo.value.datos || []);
      }

      if (eventosActivos.status === 'fulfilled') {
        const evts = (eventosActivos.value.eventos || []).map((e) => ({
          id: e.id,
          deviceId: e.device_id,
          mensaje: e.mensaje,
          tiempo: e.timestamp
            ? calcularTiempoRelativo(e.timestamp)
            : '',
        }));
        setAlertas(evts);
      }
    } catch (err) {
      console.error('Error cargando resumen:', err);
    } finally {
      setCargando(false);
    }
  };

  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Resumen general</h2>
        <p className="text-gray-500 text-sm mt-1">
          Estado operativo del sistema y visión rápida de los medidores
        </p>
      </div>

      {/* KPIs */}
      <div className="grid grid-cols-2 md:grid-cols-3 lg:grid-cols-5 gap-4 mb-6">
        <KpiCard label="Total medidores" valor={metricas.total_dispositivos} icon={Monitor} />
        <KpiCard label="Online" valor={metricas.online} icon={Wifi} />
        <KpiCard label="Offline" valor={metricas.offline} icon={WifiOff} />
        <KpiCard label="Alarmas activas" valor={metricas.alarmas_activas} icon={AlertTriangle} />
        <KpiCard label="Consumo total día" valor={metricas.consumo_total_kwh} unidad="kWh" icon={Zap} />
      </div>

      {/* Gráfica + Alertas */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        <div className="lg:col-span-2">
          <ConsumoAgregadoChart datos={consumoHorario} />
        </div>
        <div>
          <ActiveAlertsList alertas={alertas} />
        </div>
      </div>
    </div>
  );
};

function calcularTiempoRelativo(timestamp) {
  const diff = Date.now() - new Date(timestamp).getTime();
  const mins = Math.floor(diff / 60000);
  if (mins < 1) return 'Ahora';
  if (mins < 60) return `Hace ${mins} min`;
  const hrs = Math.floor(mins / 60);
  if (hrs < 24) return `Hace ${hrs}h ${mins % 60}min`;
  return `Hace ${Math.floor(hrs / 24)} días`;
}

export default Resumen;
