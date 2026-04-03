import { Bell } from 'lucide-react';

const Eventos = () => {
  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Eventos y alarmas</h2>
        <p className="text-gray-500 text-sm mt-1">
          Historial de alertas y eventos del sistema
        </p>
      </div>

      <div className="bg-white rounded-xl border border-gray-200 p-12 text-center">
        <Bell className="w-12 h-12 text-gray-300 mx-auto mb-4" />
        <h3 className="text-lg font-semibold text-gray-600 mb-2">Próximamente</h3>
        <p className="text-sm text-gray-400 max-w-md mx-auto">
          El registro de eventos y alarmas estará disponible cuando se implemente el almacenamiento
          de alertas en el backend. Las alertas incluirán sobrevoltaje, subtensión, pérdida de
          comunicación y otros eventos según la normativa CREG 024/2015.
        </p>
      </div>
    </div>
  );
};

export default Eventos;
