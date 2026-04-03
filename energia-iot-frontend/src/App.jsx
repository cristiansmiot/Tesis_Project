import { Routes, Route, Navigate } from 'react-router-dom';
import AuthLayout from './layouts/AuthLayout';
import AppLayout from './layouts/AppLayout';
import Login from './pages/Login';
import Resumen from './pages/Resumen';
import Medidores from './pages/Medidores';
import MedidorDetalle from './pages/MedidorDetalle';
import Eventos from './pages/Eventos';
import Auditoria from './pages/Auditoria';
import Perfil from './pages/Perfil';

function App() {
  return (
    <Routes>
      {/* Rutas públicas */}
      <Route element={<AuthLayout />}>
        <Route path="/login" element={<Login />} />
      </Route>

      {/* Rutas protegidas */}
      <Route element={<AppLayout />}>
        <Route path="/resumen" element={<Resumen />} />
        <Route path="/medidores" element={<Medidores />} />
        <Route path="/medidores/:deviceId" element={<MedidorDetalle />} />
        <Route path="/eventos" element={<Eventos />} />
        <Route path="/auditoria" element={<Auditoria />} />
        <Route path="/perfil" element={<Perfil />} />
      </Route>

      {/* Redirect raíz */}
      <Route path="*" element={<Navigate to="/resumen" replace />} />
    </Routes>
  );
}

export default App;
