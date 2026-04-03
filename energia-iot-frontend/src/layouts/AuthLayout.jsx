import { Outlet, Navigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';

const AuthLayout = () => {
  const { isAuthenticated, loading } = useAuth();

  if (loading) return null;
  if (isAuthenticated) return <Navigate to="/resumen" replace />;

  return (
    <div className="min-h-screen bg-gray-50 flex items-center justify-center p-4">
      <Outlet />
    </div>
  );
};

export default AuthLayout;
