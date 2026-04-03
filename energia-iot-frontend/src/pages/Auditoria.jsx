import { FileText } from 'lucide-react';

const Auditoria = () => {
  return (
    <div>
      <div className="mb-6">
        <h2 className="text-2xl font-bold text-gray-800">Auditoría</h2>
        <p className="text-gray-500 text-sm mt-1">
          Registro de actividades y cambios en el sistema
        </p>
      </div>

      <div className="bg-white rounded-xl border border-gray-200 p-12 text-center">
        <FileText className="w-12 h-12 text-gray-300 mx-auto mb-4" />
        <h3 className="text-lg font-semibold text-gray-600 mb-2">Próximamente</h3>
        <p className="text-sm text-gray-400 max-w-md mx-auto">
          El registro de auditoría estará disponible cuando se implemente la autenticación y el
          logging de actividades en el backend. Registrará comandos enviados, alertas reconocidas,
          dispositivos editados y eventos de login/logout.
        </p>
      </div>
    </div>
  );
};

export default Auditoria;
