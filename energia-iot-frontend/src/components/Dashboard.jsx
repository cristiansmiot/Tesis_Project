import { useState, useEffect } from 'react';
import { RefreshCw, AlertCircle, CheckCircle } from 'lucide-react';
import IndicadorEnergia from './IndicadorEnergia';
import GraficoConsumo from './GraficoConsumo';
import EstadoDispositivo from './EstadoDispositivo';
import PanelComandos from './PanelComandos';
import MetricasTransmision from './MetricasTransmision';
import TablaHistorial from './TablaHistorial';
import { dispositivosAPI, medicionesAPI, saludAPI, healthCheck } from '../services/api';

/**
 * Dashboard principal del sistema de medición de energía
 */
const Dashboard = () => {
  // Estados
  const [dispositivo, setDispositivo] = useState(null);
  const [estadoDispositivo, setEstadoDispositivo] = useState(null);
  const [saludDispositivo, setSaludDispositivo] = useState(null);
  const [metricasTransmision, setMetricasTransmision] = useState(null);
  const [ultimaMedicion, setUltimaMedicion] = useState(null);
  const [historial, setHistorial] = useState([]);
  const [estadisticas, setEstadisticas] = useState(null);
  const [cargando, setCargando] = useState(true);
  const [error, setError] = useState(null);
  const [backendConectado, setBackendConectado] = useState(false);
  const [ultimaActualizacion, setUltimaActualizacion] = useState(null);

  // ID del dispositivo (en producción, esto vendría de configuración o selección)
  const DEVICE_ID = 'ESP32-001';

  // Función para cargar todos los datos
  const cargarDatos = async () => {
    try {
      setCargando(true);
      setError(null);

      // Verificar conexión con backend
      const health = await healthCheck();
      setBackendConectado(health.status === 'healthy');

      if (health.status !== 'healthy') {
        throw new Error('No se puede conectar con el backend. ¿Está ejecutándose uvicorn?');
      }

      // Cargar dispositivo
      try {
        const disp = await dispositivosAPI.obtener(DEVICE_ID);
        setDispositivo(disp);
      } catch (e) {
        console.log('Dispositivo no encontrado, se creará con la primera medición');
      }

      // Cargar estado del dispositivo
      try {
        const estado = await dispositivosAPI.estado(DEVICE_ID);
        setEstadoDispositivo(estado);
      } catch (e) {
        console.log('Estado no disponible');
      }

      // Cargar salud del nodo (métricas e indicadores del topic /estado)
      try {
        const salud = await dispositivosAPI.salud(DEVICE_ID);
        setSaludDispositivo(salud);
      } catch (e) {
        console.log('Salud no disponible aún');
      }

      // Cargar métricas de transmisión (PDR, RSSI, reconexiones)
      try {
        const metricas = await saludAPI.metricas(DEVICE_ID, 24);
        setMetricasTransmision(metricas);
      } catch (e) {
        console.log('Métricas de transmisión no disponibles aún');
      }

      // Cargar última medición
      try {
        const ultima = await medicionesAPI.ultima(DEVICE_ID);
        setUltimaMedicion(ultima);
      } catch (e) {
        console.log('No hay mediciones aún');
      }

      // Cargar histórico
      try {
        const hist = await medicionesAPI.historico(DEVICE_ID, 24);
        setHistorial(hist.mediciones || []);
        setEstadisticas(hist.estadisticas);
      } catch (e) {
        console.log('Histórico no disponible');
        setHistorial([]);
      }

      setUltimaActualizacion(new Date());
    } catch (err) {
      console.error('Error cargando datos:', err);
      setError(err.message);
      setBackendConectado(false);
    } finally {
      setCargando(false);
    }
  };

  // Cargar datos al inicio
  useEffect(() => {
    cargarDatos();
    
    // Auto-actualizar cada 30 segundos
    const intervalo = setInterval(cargarDatos, 30000);
    return () => clearInterval(intervalo);
  }, []);

  // Formatear valores de la última medición
  const voltaje = ultimaMedicion?.voltaje_rms?.toFixed(1) || '---';
  const corriente = ultimaMedicion?.corriente_rms?.toFixed(3) || '---';
  const potencia = ultimaMedicion?.potencia_activa?.toFixed(1) || '---';
  const factorPotencia = ultimaMedicion?.factor_potencia?.toFixed(2) || '---';
  const energia = ultimaMedicion?.energia_activa?.toFixed(3) || '0.000';

  return (
    <div className="min-h-screen bg-gray-100">
      {/* Header */}
      <header className="header-gradient text-white py-6 px-4 shadow-lg">
        <div className="max-w-7xl mx-auto">
          <div className="flex items-center justify-between">
            <div>
              <h1 className="text-2xl md:text-3xl font-bold flex items-center gap-2">
                ⚡ Medidor de Energía IoT
              </h1>
              <p className="text-blue-100 mt-1">
                Tesis de Maestría - Pontificia Universidad Javeriana
              </p>
            </div>
            
            <div className="flex items-center gap-4">
              {/* Estado del backend */}
              <div className={`flex items-center gap-2 px-3 py-1 rounded-full ${
                backendConectado ? 'bg-green-500/20' : 'bg-red-500/20'
              }`}>
                {backendConectado ? (
                  <CheckCircle className="w-4 h-4 text-green-300" />
                ) : (
                  <AlertCircle className="w-4 h-4 text-red-300" />
                )}
                <span className="text-sm">
                  {backendConectado ? 'Backend OK' : 'Sin conexión'}
                </span>
              </div>

              {/* Botón actualizar */}
              <button
                onClick={cargarDatos}
                disabled={cargando}
                className="flex items-center gap-2 bg-white/10 hover:bg-white/20 px-4 py-2 rounded-lg transition-colors disabled:opacity-50"
              >
                <RefreshCw className={`w-4 h-4 ${cargando ? 'animate-spin' : ''}`} />
                Actualizar
              </button>
            </div>
          </div>
        </div>
      </header>

      {/* Contenido principal */}
      <main className="max-w-7xl mx-auto px-4 py-6">
        {/* Error */}
        {error && (
          <div className="mb-6 bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded-lg flex items-center gap-2">
            <AlertCircle className="w-5 h-5" />
            <span>{error}</span>
          </div>
        )}

        {/* Indicadores principales */}
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4 mb-6">
          <IndicadorEnergia
            tipo="voltaje"
            valor={voltaje}
            unidad="V"
            label="Voltaje RMS"
            descripcion={estadisticas ? `Rango: ${estadisticas.voltaje_minimo}-${estadisticas.voltaje_maximo} V` : null}
          />
          <IndicadorEnergia
            tipo="corriente"
            valor={corriente}
            unidad="A"
            label="Corriente RMS"
          />
          <IndicadorEnergia
            tipo="potencia"
            valor={potencia}
            unidad="W"
            label="Potencia Activa"
            descripcion={estadisticas ? `Máx: ${estadisticas.potencia_maxima} W` : null}
          />
          <IndicadorEnergia
            tipo="factor"
            valor={factorPotencia}
            unidad=""
            label="Factor de Potencia"
          />
        </div>

        {/* Energía acumulada */}
        <div className="mb-6">
          <div className="bg-gradient-to-r from-emerald-500 to-teal-600 rounded-xl shadow-md p-6 text-white">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-emerald-100 text-sm">Energía Consumida</p>
                <div className="flex items-baseline gap-2 mt-1">
                  <span className="text-4xl font-bold">{energia}</span>
                  <span className="text-xl text-emerald-100">kWh</span>
                </div>
              </div>
              <div className="text-right">
                <p className="text-emerald-100 text-sm">Costo estimado</p>
                <p className="text-2xl font-bold">
                  ${(parseFloat(energia) * 850).toFixed(0)} COP
                </p>
                <p className="text-xs text-emerald-200">~$850/kWh</p>
              </div>
            </div>
          </div>
        </div>

        {/* Grid de dos columnas */}
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6 mb-6">
          {/* Gráfico (ocupa 2 columnas) */}
          <div className="lg:col-span-2">
            <GraficoConsumo datos={historial} />
          </div>

          {/* Estado del dispositivo + Comandos remotos */}
          <div className="space-y-6">
            <EstadoDispositivo
              dispositivo={dispositivo}
              estado={estadoDispositivo}
              salud={saludDispositivo}
            />
            <PanelComandos
              deviceId={DEVICE_ID}
              conectado={estadoDispositivo?.conectado || false}
            />
          </div>
        </div>

        {/* Métricas de transmisión */}
        <div className="mb-6">
          <MetricasTransmision metricas={metricasTransmision} />
        </div>

        {/* Tabla de historial */}
        <TablaHistorial mediciones={historial} limite={10} />

        {/* Footer con última actualización */}
        <div className="text-center text-sm text-gray-400 mt-6">
          {ultimaActualizacion && (
            <p>
              Última actualización: {ultimaActualizacion.toLocaleTimeString('es-CO')}
              {' | '}Auto-actualización cada 30 segundos
            </p>
          )}
        </div>
      </main>
    </div>
  );
};

export default Dashboard;
