import clsx from 'clsx';

const KpiCard = ({ label, valor, unidad, icon: Icon, className }) => {
  return (
    <div className={clsx('bg-white rounded-xl border border-gray-200 p-5', className)}>
      <div className="flex items-center justify-between mb-2">
        <span className="text-sm text-gray-500">{label}</span>
        {Icon && <Icon className="w-4 h-4 text-gray-400" />}
      </div>
      <div className="flex items-baseline gap-1">
        <span className="text-2xl font-bold text-gray-900">{valor}</span>
        {unidad && <span className="text-sm text-gray-500">{unidad}</span>}
      </div>
    </div>
  );
};

export default KpiCard;
