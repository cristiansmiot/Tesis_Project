/**
 * Panel lateral con información del dispositivo.
 * Nombre/ubicación/etiqueta son administrativos (editables); serial,
 * firmware y tipo de conexión los reporta el propio equipo por MQTT.
 * Props: dispositivo, salud (registro /estado más reciente)
 */
const DeviceInfoSidebar = ({ dispositivo, salud }) => {
  const campos = [
    { label: 'Nombre', valor: dispositivo?.nombre || 'Sin nombre' },
    { label: 'ID', valor: dispositivo?.device_id || 'N/A' },
    { label: 'Serial (IMEI)', valor: salud?.imei || 'Sin reportar' },
    { label: 'Ubicación', valor: dispositivo?.ubicacion || 'Sin ubicación' },
    { label: 'Etiqueta', valor: dispositivo?.etiqueta || '—' },
    { label: 'Firmware', valor: salud?.fw_version || dispositivo?.firmware_version || 'Sin reportar' },
    { label: 'Tipo de conexión', valor: 'Celular LTE-M/NB-IoT (SIM7080G)' },
    {
      label: 'Última conexión',
      valor: dispositivo?.ultima_conexion
        ? new Date(dispositivo.ultima_conexion).toLocaleString('es-CO', {
            day: '2-digit', month: '2-digit', year: 'numeric',
            hour: '2-digit', minute: '2-digit',
          })
        : 'Nunca',
    },
  ];

  return (
    <div className="bg-white rounded-xl border border-gray-200 p-6">
      <h4 className="text-base font-semibold text-gray-800 mb-4">Información del dispositivo</h4>
      <div className="space-y-4">
        {campos.map(({ label, valor }) => (
          <div key={label}>
            <p className="text-xs text-gray-500">{label}</p>
            <p className="text-sm font-medium text-gray-800">{valor}</p>
          </div>
        ))}
      </div>
    </div>
  );
};

export default DeviceInfoSidebar;
