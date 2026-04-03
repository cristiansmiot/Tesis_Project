import { useState, useEffect } from 'react';
import { useOutletContext } from 'react-router-dom';
import { Monitor, Wifi, WifiOff, AlertTriangle, Zap } from 'lucide-react';
import KpiCard from '../components/common/KpiCard';
import ConsumoAgregadoChart from '../components/charts/ConsumoAgregadoChart';
import ActiveAlertsList from '../components/alerts/ActiveAlertsList';
import { dispositivosAPI, medicionesAPI, saludAPI } from '../services/api';
import { evaluarVoltaje, evaluarConectividad } from '../utils/voltage';

const Resumen = () => {
  const { refreshKey } = useOutletContext();
  const [dispositivos, setDispositivos] = useState([]);
  const [metricas, setMetricas] = useState({ total: 0, online: 0, offline: 0, alarmas: 0, consumoTotal: 0 });
  const [consumoHorario, setConsumoHorario] = useState([]);
  const [alertas, setAlertas] = useState([]);
  const [cargando, setCargando] = useState(true);

  useEffect(() => {
    cargarDatos();
  }, [refreshKey]);

  const cargarDatos = async () => {
    setCargando(true);
    try {
      // Obtener todos los dispositivos
      const disps = await dispositivosAPI.listar();
      setDispositivos(disps);

      const online = disps.filter((d) => d.conectado).length;
      const offline = disps.length - online;

      // Obtener última medición y salud de cada dispositivo
      const alertasTemp = [];
      let consumoTotal = 0;

      const resultados = await Promise.allSettled(
        disps.map(async (d) => {
          const resultado = { deviceId: d.device_id, nombre: d.nombre };

          // Última medición
          try {
            const ultima = await medicionesAPI.ultima(d.device_id);
            resultado.medicion = ultima;
            consumoTotal += ultima.energia_activa || 0;

            // Verificar voltaje CREG 024/2015
            const voltStatus = evaluarVoltaje(ultima.voltaje_rms);
            if (voltStatus.estado !== 'normal' && voltStatus.estado !== 'desconocido') {
              alertasTemp.push({
                deviceId: d.device_id,
                mensaje: `${voltStatus.label} (${ultima.voltaje_rms?.toFixed(1)} V)`,
                tiempo: new Date(ultima.timestamp).toLocaleString('es-CO'),
              });
            }
          } catch {
            // Sin mediciones
          }

          // Verificar conectividad
          if (!d.conectado) {
            const tiempoDesconectado = d.ultima_conexion
              ? calcularTiempoRelativo(d.ultima_conexion)
              : 'Nunca conectado';
            alertasTemp.push({
              deviceId: d.device_id,
              mensaje: 'Sin reporte',
              tiempo: tiempoDesconectado,
            });
          }

          // Salud (RSSI débil)
          try {
            const salud = await dispositivosAPI.salud(d.device_id);
            const rssiStatus = evaluarConectividad(salud?.rssi_dbm);
            if (rssiStatus.nivel === 'Baja') {
              alertasTemp.push({
                deviceId: d.device_id,
                mensaje: `Comunicación débil (${salud.rssi_dbm} dBm)`,
                tiempo: new Date(salud.timestamp).toLocaleString('es-CO'),
              });
            }
          } catch {
            // Sin salud
          }

          return resultado;
        })
      );

      setMetricas({
        total: disps.length,
        online,
        offline,
        alarmas: alertasTemp.length,
        consumoTotal: consumoTotal.toFixed(1),
      });
      setAlertas(alertasTemp);

      // Generar datos de consumo horario agregado (simplificado)
      await cargarConsumoHorario(disps);
    } catch (err) {
      console.error('Error cargando resumen:', err);
    } finally {
      setCargando(false);
    }
  };

  const cargarConsumoHorario = async (disps) => {
    // Agregar datos históricos de todos los dispositivos por hora
    const horasBucket = {};
    for (let h = 0; h < 24; h++) {
      const hora = `${h.toString().padStart(2, '0')}:00`;
      horasBucket[hora] = 0;
    }

    const results = await Promise.allSettled(
      disps.slice(0, 10).map((d) => medicionesAPI.historico(d.device_id, 24))
    );

    results.forEach((r) => {
      if (r.status === 'fulfilled' && r.value?.mediciones) {
        r.value.mediciones.forEach((m) => {
          const h = new Date(m.timestamp).getHours();
          const hora = `${h.toString().padStart(2, '0')}:00`;
          if (horasBucket[hora] !== undefined) {
            horasBucket[hora] += (m.potencia_activa || 0) / 1000; // W → kW, simplificado
          }
        });
      }
    });

    const datosGrafica = Object.entries(horasBucket).map(([hora, consumo]) => ({
      hora,
      consumo: parseFloat(consumo.toFixed(2)),
    }));
    setConsumoHorario(datosGrafica);
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
        <KpiCard label="Total medidores" valor={metricas.total} icon={Monitor} />
        <KpiCard label="Online" valor={metricas.online} icon={Wifi} />
        <KpiCard label="Offline" valor={metricas.offline} icon={WifiOff} />
        <KpiCard label="Alarmas activas" valor={metricas.alarmas} icon={AlertTriangle} />
        <KpiCard label="Consumo total día" valor={metricas.consumoTotal} unidad="kWh" icon={Zap} />
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
  if (mins < 60) return `Hace ${mins} min`;
  const hrs = Math.floor(mins / 60);
  if (hrs < 24) return `Hace ${hrs}h ${mins % 60}min`;
  return `Hace ${Math.floor(hrs / 24)} días`;
}

export default Resumen;
