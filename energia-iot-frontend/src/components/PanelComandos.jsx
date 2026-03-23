import { useState } from 'react';
import { Send, RotateCcw, Search, Gauge, Loader2 } from 'lucide-react';
import { dispositivosAPI } from '../services/api';

const COMANDOS = [
  {
    id: 'calibrate',
    label: 'Calibrar ADE',
    descripcion: 'Inicia calibracion mSure del ADE9153A',
    icon: Gauge,
    color: 'blue',
  },
  {
    id: 'probe',
    label: 'Probe ADE',
    descripcion: 'Verifica comunicacion SPI con el ADE9153A',
    icon: Search,
    color: 'amber',
  },
  {
    id: 'reset',
    label: 'Reiniciar ESP32',
    descripcion: 'Reinicia el microcontrolador remotamente',
    icon: RotateCcw,
    color: 'red',
  },
];

const colorClasses = {
  blue: {
    bg: 'bg-blue-50 hover:bg-blue-100',
    icon: 'text-blue-600',
    border: 'border-blue-200',
    loading: 'text-blue-500',
  },
  amber: {
    bg: 'bg-amber-50 hover:bg-amber-100',
    icon: 'text-amber-600',
    border: 'border-amber-200',
    loading: 'text-amber-500',
  },
  red: {
    bg: 'bg-red-50 hover:bg-red-100',
    icon: 'text-red-600',
    border: 'border-red-200',
    loading: 'text-red-500',
  },
};

const PanelComandos = ({ deviceId, conectado }) => {
  const [enviando, setEnviando] = useState(null);
  const [resultado, setResultado] = useState(null);

  const enviarComando = async (comandoId) => {
    if (enviando) return;

    if (comandoId === 'reset') {
      if (!window.confirm('Reiniciar el ESP32 remotamente?')) return;
    }

    setEnviando(comandoId);
    setResultado(null);

    try {
      const res = await dispositivosAPI.enviarComando(deviceId, comandoId);
      setResultado({ ok: true, mensaje: res.mensaje });
    } catch (err) {
      setResultado({ ok: false, mensaje: err.message });
    } finally {
      setEnviando(null);
      setTimeout(() => setResultado(null), 5000);
    }
  };

  return (
    <div className="bg-white rounded-xl shadow-md p-6">
      <div className="flex items-center gap-2 mb-4">
        <Send className="w-5 h-5 text-gray-600" />
        <h3 className="text-lg font-semibold text-gray-700">Comandos Remotos</h3>
      </div>

      {!conectado && (
        <p className="text-sm text-amber-600 bg-amber-50 rounded-lg px-3 py-2 mb-4">
          Dispositivo desconectado. Los comandos se enviaran pero podrian no ser recibidos.
        </p>
      )}

      <div className="space-y-3">
        {COMANDOS.map((cmd) => {
          const Icon = cmd.icon;
          const cls = colorClasses[cmd.color];
          const isLoading = enviando === cmd.id;

          return (
            <button
              key={cmd.id}
              onClick={() => enviarComando(cmd.id)}
              disabled={!!enviando}
              className={`w-full flex items-center gap-3 p-3 rounded-lg border transition-colors disabled:opacity-50 ${cls.bg} ${cls.border}`}
            >
              {isLoading ? (
                <Loader2 className={`w-5 h-5 animate-spin ${cls.loading}`} />
              ) : (
                <Icon className={`w-5 h-5 ${cls.icon}`} />
              )}
              <div className="text-left">
                <p className="text-sm font-medium text-gray-800">{cmd.label}</p>
                <p className="text-xs text-gray-500">{cmd.descripcion}</p>
              </div>
            </button>
          );
        })}
      </div>

      {resultado && (
        <div className={`mt-4 text-sm px-3 py-2 rounded-lg ${
          resultado.ok ? 'bg-green-50 text-green-700' : 'bg-red-50 text-red-700'
        }`}>
          {resultado.mensaje}
        </div>
      )}
    </div>
  );
};

export default PanelComandos;
