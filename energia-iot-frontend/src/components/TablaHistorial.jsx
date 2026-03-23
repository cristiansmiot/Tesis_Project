/**
 * Componente que muestra una tabla con el historial de mediciones
 */
const TablaHistorial = ({ mediciones, limite = 10 }) => {
  const formatearFecha = (timestamp) => {
    const fecha = new Date(timestamp);
    return fecha.toLocaleString('es-CO', {
      day: '2-digit',
      month: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  };

  const medicionesLimitadas = mediciones.slice(0, limite);

  return (
    <div className="bg-white rounded-xl shadow-md p-6">
      <h3 className="text-lg font-semibold text-gray-700 mb-4">
        📋 Últimas Mediciones
      </h3>

      {medicionesLimitadas.length === 0 ? (
        <div className="text-center py-8 text-gray-400">
          No hay mediciones registradas
        </div>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-gray-200">
                <th className="text-left py-3 px-2 font-semibold text-gray-600">Hora</th>
                <th className="text-right py-3 px-2 font-semibold text-gray-600">Voltaje</th>
                <th className="text-right py-3 px-2 font-semibold text-gray-600">Corriente</th>
                <th className="text-right py-3 px-2 font-semibold text-gray-600">Potencia</th>
                <th className="text-right py-3 px-2 font-semibold text-gray-600">FP</th>
              </tr>
            </thead>
            <tbody>
              {medicionesLimitadas.map((medicion, index) => (
                <tr 
                  key={medicion.id || index} 
                  className="border-b border-gray-100 hover:bg-gray-50 transition-colors"
                >
                  <td className="py-3 px-2 text-gray-600">
                    {formatearFecha(medicion.timestamp)}
                  </td>
                  <td className="py-3 px-2 text-right font-mono">
                    <span className="text-yellow-600">{medicion.voltaje_rms?.toFixed(1)}</span>
                    <span className="text-gray-400 ml-1">V</span>
                  </td>
                  <td className="py-3 px-2 text-right font-mono">
                    <span className="text-blue-600">{medicion.corriente_rms?.toFixed(3)}</span>
                    <span className="text-gray-400 ml-1">A</span>
                  </td>
                  <td className="py-3 px-2 text-right font-mono">
                    <span className="text-green-600">{medicion.potencia_activa?.toFixed(1)}</span>
                    <span className="text-gray-400 ml-1">W</span>
                  </td>
                  <td className="py-3 px-2 text-right font-mono">
                    <span className="text-purple-600">{medicion.factor_potencia?.toFixed(2)}</span>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {mediciones.length > limite && (
        <p className="text-center text-sm text-gray-400 mt-4">
          Mostrando {limite} de {mediciones.length} mediciones
        </p>
      )}
    </div>
  );
};

export default TablaHistorial;
