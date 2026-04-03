import { useState, useEffect } from 'react';
import { useParams, useOutletContext, Link } from 'react-router-dom';
import { MapPin, Clock, Cpu, Edit } from 'lucide-react';
import StatusBadge from '../components/common/StatusBadge';
import TabNav from '../components/common/TabNav';
import DeviceKpiCards from '../components/device/DeviceKpiCards';
import DeviceInfoSidebar from '../components/device/DeviceInfoSidebar';
import DeviceIndicators from '../components/device/DeviceIndicators';
import ConsumoHistoricoChart from '../components/charts/ConsumoHistoricoChart';
import MetricasTransmision from '../components/MetricasTransmision';
import TablaHistorial from '../components/TablaHistorial';
import { dispositivosAPI, medicionesAPI, saludAPI } from '../services/api';

const tabs = [
  { id: 'resumen', label: 'Resumen' },
  { id: 'variables', label: 'Variables' },
  { id: 'historico', label: 'Histórico' },
  { id: 'comandos', label: 'Comandos' },
  { id: 'eventos', label: 'Eventos' },
];

const MedidorDetalle = () => {
  const { deviceId } = useParams();
  const { refreshKey } = useOutletContext();
  const [activeTab, setActiveTab] = useState('resumen');
  const [dispositivo, setDispositivo] = useState(null);
  const [medicion, setMedicion] = useState(null);
  const [historial, setHistorial] = useState([]);
  const [estadisticas, setEstadisticas] = useState(null);
  const [saludData, setSaludData] = useState(null);
  const [metricasTransmision, setMetricasTransmision] = useState(null);
  const [cargando, setCargando] = useState(true);

  useEffect(() => {
    cargarDatos();
  }, [deviceId, refreshKey]);

  const cargarDatos = async () => {
    setCargando(true);
    try {
      const resultados = await Promise.allSettled([
        dispositivosAPI.obtener(deviceId),
        medicionesAPI.ultima(deviceId),
        medicionesAPI.historico(deviceId, 24),
        dispositivosAPI.salud(deviceId),
        saludAPI.metricas(deviceId, 24),
      ]);

      if (resultados[0].status === 'fulfilled') setDispositivo(resultados[0].value);
      if (resultados[1].status === 'fulfilled') setMedicion(resultados[1].value);
      if (resultados[2].status === 'fulfilled') {
        setHistorial(resultados[2].value.mediciones || []);
        setEstadisticas(resultados[2].value.estadisticas);
      }
      if (resultados[3].status === 'fulfilled') setSaludData(resultados[3].value);
      if (resultados[4].status === 'fulfilled') setMetricasTransmision(resultados[4].value);
    } catch (err) {
      console.error('Error cargando detalle:', err);
    } finally {
      setCargando(false);
    }
  };

  const ultimaConexion = dispositivo?.ultima_conexion
    ? new Date(dispositivo.ultima_conexion).toLocaleTimeString('es-CO', { hour: '2-digit', minute: '2-digit' })
    : 'N/A';

  return (
    <div>
      {/* Header */}
      <div className="mb-6">
        <div className="flex items-center justify-between">
          <div>
            <h2 className="text-2xl font-bold text-gray-800">
              Medidor {deviceId}
            </h2>
            <div className="flex items-center gap-4 mt-2 text-sm text-gray-500">
              {dispositivo?.ubicacion && (
                <span className="flex items-center gap-1">
                  <MapPin className="w-4 h-4" />
                  {dispositivo.ubicacion}
                </span>
              )}
              <span className="flex items-center gap-1">
                <Clock className="w-4 h-4" />
                Última conexión: {ultimaConexion}
              </span>
              {dispositivo?.firmware_version && (
                <span className="flex items-center gap-1">
                  <Cpu className="w-4 h-4" />
                  Firmware: {dispositivo.firmware_version}
                </span>
              )}
            </div>
          </div>
          <div className="flex items-center gap-3">
            <StatusBadge
              label={dispositivo?.conectado ? 'Online' : 'Offline'}
              color={dispositivo?.conectado ? 'green' : 'red'}
            />
            <button className="px-4 py-2 border border-gray-300 rounded-lg text-sm font-medium text-gray-700 hover:bg-gray-50 flex items-center gap-2">
              <Edit className="w-4 h-4" />
              Editar
            </button>
          </div>
        </div>
      </div>

      {/* KPIs */}
      <div className="mb-6">
        <DeviceKpiCards medicion={medicion} />
      </div>

      {/* Tabs */}
      <div className="mb-6">
        <TabNav tabs={tabs} activeTab={activeTab} onChange={setActiveTab} />
      </div>

      {/* Contenido de tabs */}
      {activeTab === 'resumen' && (
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          <div className="lg:col-span-2">
            <DeviceIndicators medicion={medicion} />
          </div>
          <div>
            <DeviceInfoSidebar dispositivo={dispositivo} medicion={medicion} />
          </div>
        </div>
      )}

      {activeTab === 'variables' && (
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
          <div className="lg:col-span-2">
            <DeviceIndicators medicion={medicion} />
          </div>
          <div className="space-y-6">
            <MetricasTransmision metricas={metricasTransmision} />
          </div>
        </div>
      )}

      {activeTab === 'historico' && (
        <div className="space-y-6">
          <ConsumoHistoricoChart datos={historial} />
          <TablaHistorial mediciones={historial} limite={20} />
        </div>
      )}

      {activeTab === 'comandos' && (
        <div className="bg-white rounded-xl border border-gray-200 p-6">
          <h4 className="text-base font-semibold text-gray-800 mb-4">Comandos disponibles</h4>
          <p className="text-sm text-gray-500 mb-6">
            Envía comandos al medidor a través de MQTT. Los comandos se ejecutan en el siguiente ciclo de comunicación del dispositivo.
          </p>
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            <CommandButton
              label="Reiniciar nodo"
              description="Reinicia el microcontrolador ESP32"
              color="blue"
              disabled
            />
            <CommandButton
              label="Corte de suministro"
              description="Abre el relé de corte de energía"
              color="red"
              disabled
            />
            <CommandButton
              label="Sincronizar hora"
              description="Sincroniza el reloj del dispositivo con el servidor"
              color="green"
              disabled
            />
          </div>
          <p className="text-xs text-gray-400 mt-4">
            Los comandos estarán disponibles cuando se implemente el endpoint de control en el backend (Fase 2).
          </p>
        </div>
      )}

      {activeTab === 'eventos' && (
        <div className="bg-white rounded-xl border border-gray-200 p-6">
          <h4 className="text-base font-semibold text-gray-800 mb-2">Eventos del dispositivo</h4>
          <p className="text-sm text-gray-500">
            El historial de eventos y alarmas de este dispositivo estará disponible cuando se implemente el almacenamiento de alertas en el backend (Fase 2).
          </p>
        </div>
      )}
    </div>
  );
};

const CommandButton = ({ label, description, color, disabled }) => {
  const colors = {
    blue: 'bg-blue-600 hover:bg-blue-700',
    red: 'bg-red-600 hover:bg-red-700',
    green: 'bg-green-600 hover:bg-green-700',
  };

  return (
    <div className="border border-gray-200 rounded-lg p-4">
      <h5 className="font-medium text-gray-800 text-sm">{label}</h5>
      <p className="text-xs text-gray-500 mt-1 mb-3">{description}</p>
      <button
        disabled={disabled}
        className={`w-full py-2 rounded-lg text-white text-sm font-medium transition-colors ${
          disabled ? 'bg-gray-300 cursor-not-allowed' : colors[color]
        }`}
      >
        {disabled ? 'No disponible' : 'Ejecutar'}
      </button>
    </div>
  );
};

export default MedidorDetalle;
