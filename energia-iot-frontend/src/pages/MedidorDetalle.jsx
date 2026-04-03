import { useState, useEffect } from 'react';
import { useParams, useOutletContext } from 'react-router-dom';
import { MapPin, Clock, Cpu, Edit } from 'lucide-react';
import StatusBadge from '../components/common/StatusBadge';
import TabNav from '../components/common/TabNav';
import DeviceKpiCards from '../components/device/DeviceKpiCards';
import DeviceInfoSidebar from '../components/device/DeviceInfoSidebar';
import DeviceIndicators from '../components/device/DeviceIndicators';
import ConsumoHistoricoChart from '../components/charts/ConsumoHistoricoChart';
import MetricasTransmision from '../components/MetricasTransmision';
import TablaHistorial from '../components/TablaHistorial';
import { dispositivosAPI, medicionesAPI, saludAPI, comandosAPI, eventosAPI } from '../services/api';

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
  const [eventosDevice, setEventosDevice] = useState([]);
  const [cargando, setCargando] = useState(true);
  const [comandoEnviando, setComandoEnviando] = useState(null);
  const [comandoMensaje, setComandoMensaje] = useState('');

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
        eventosAPI.listar({ device_id: deviceId, limit: 20 }),
      ]);

      if (resultados[0].status === 'fulfilled') setDispositivo(resultados[0].value);
      if (resultados[1].status === 'fulfilled') setMedicion(resultados[1].value);
      if (resultados[2].status === 'fulfilled') {
        setHistorial(resultados[2].value.mediciones || []);
        setEstadisticas(resultados[2].value.estadisticas);
      }
      if (resultados[3].status === 'fulfilled') setSaludData(resultados[3].value);
      if (resultados[4].status === 'fulfilled') setMetricasTransmision(resultados[4].value);
      if (resultados[5].status === 'fulfilled') setEventosDevice(resultados[5].value.eventos || []);
    } catch (err) {
      console.error('Error cargando detalle:', err);
    } finally {
      setCargando(false);
    }
  };

  const enviarComando = async (comando) => {
    setComandoEnviando(comando);
    setComandoMensaje('');
    try {
      const res = await comandosAPI.enviar(deviceId, comando);
      setComandoMensaje(`${res.mensaje}`);
    } catch (err) {
      setComandoMensaje(`Error: ${err.message}`);
    } finally {
      setComandoEnviando(null);
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

      {/* Tab: Resumen */}
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

      {/* Tab: Variables */}
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

      {/* Tab: Histórico */}
      {activeTab === 'historico' && (
        <div className="space-y-6">
          <ConsumoHistoricoChart datos={historial} />
          <TablaHistorial mediciones={historial} limite={20} />
        </div>
      )}

      {/* Tab: Comandos */}
      {activeTab === 'comandos' && (
        <div className="bg-white rounded-xl border border-gray-200 p-6">
          <h4 className="text-base font-semibold text-gray-800 mb-4">Comandos disponibles</h4>
          <p className="text-sm text-gray-500 mb-6">
            Envía comandos al medidor a través de MQTT. Los comandos se ejecutan en el siguiente ciclo de comunicación del dispositivo.
          </p>

          {comandoMensaje && (
            <div className={`mb-4 p-3 rounded-lg text-sm ${
              comandoMensaje.startsWith('Error')
                ? 'bg-red-50 text-red-700'
                : 'bg-green-50 text-green-700'
            }`}>
              {comandoMensaje}
            </div>
          )}

          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            <CommandButton
              label="Reiniciar nodo"
              description="Reinicia el microcontrolador ESP32"
              color="blue"
              loading={comandoEnviando === 'reiniciar'}
              onClick={() => enviarComando('reiniciar')}
            />
            <CommandButton
              label="Corte de suministro"
              description="Abre el relé de corte de energía"
              color="red"
              loading={comandoEnviando === 'corte_suministro'}
              onClick={() => {
                if (confirm('¿Está seguro de enviar un comando de corte de suministro?')) {
                  enviarComando('corte_suministro');
                }
              }}
            />
            <CommandButton
              label="Sincronizar hora"
              description="Sincroniza el reloj del dispositivo con el servidor"
              color="green"
              loading={comandoEnviando === 'sincronizar_hora'}
              onClick={() => enviarComando('sincronizar_hora')}
            />
          </div>
          <p className="text-xs text-gray-400 mt-4">
            Nota: El envío del comando no garantiza su ejecución. El dispositivo debe estar conectado y procesará el comando en su siguiente ciclo.
          </p>
        </div>
      )}

      {/* Tab: Eventos */}
      {activeTab === 'eventos' && (
        <div className="bg-white rounded-xl border border-gray-200 p-6">
          <h4 className="text-base font-semibold text-gray-800 mb-4">
            Eventos del dispositivo ({eventosDevice.length})
          </h4>
          {eventosDevice.length === 0 ? (
            <p className="text-sm text-gray-400">No hay eventos registrados para este dispositivo.</p>
          ) : (
            <div className="space-y-3">
              {eventosDevice.map((e) => (
                <div key={e.id} className={`flex items-start gap-3 p-3 rounded-lg border ${
                  e.activo ? 'bg-red-50 border-red-100' : 'bg-gray-50 border-gray-100'
                }`}>
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-2 mb-1">
                      <StatusBadge
                        label={e.severidad}
                        color={e.severidad === 'critical' ? 'red' : e.severidad === 'warning' ? 'yellow' : 'blue'}
                        showDot={false}
                      />
                      <span className="text-xs text-gray-400">
                        {e.timestamp ? new Date(e.timestamp).toLocaleString('es-CO') : ''}
                      </span>
                    </div>
                    <p className="text-sm text-gray-700">{e.mensaje}</p>
                    {e.valor != null && (
                      <p className="text-xs text-gray-500 mt-1">
                        Valor: {e.valor.toFixed(1)} | Umbral: {e.umbral?.toFixed(0) || '—'}
                      </p>
                    )}
                  </div>
                  {e.activo ? (
                    <StatusBadge label="Activa" color="red" />
                  ) : (
                    <StatusBadge label="Reconocida" color="green" />
                  )}
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
};

const CommandButton = ({ label, description, color, loading, onClick }) => {
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
        disabled={loading}
        onClick={onClick}
        className={`w-full py-2 rounded-lg text-white text-sm font-medium transition-colors ${
          loading ? 'bg-gray-300 cursor-wait' : colors[color]
        }`}
      >
        {loading ? 'Enviando...' : 'Ejecutar'}
      </button>
    </div>
  );
};

export default MedidorDetalle;
