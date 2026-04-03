/**
 * Panel lateral con información del dispositivo.
 * Props: dispositivo, medicion
 */
const DeviceInfoSidebar = ({ dispositivo, medicion }) => {
  const campos = [
    { label: 'Nombre', valor: dispositivo?.nombre || 'Sin nombre' },
    { label: 'ID', valor: dispositivo?.device_id || 'N/A' },
    { label: 'Ubicación', valor: dispositivo?.ubicacion || 'Sin ubicación' },
    { label: 'Firmware', valor: dispositivo?.firmware_version || 'N/A' },
    { label: 'Tipo de conexión', valor: 'WiFi' },
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
