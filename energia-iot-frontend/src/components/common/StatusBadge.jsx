import clsx from 'clsx';

const colorMap = {
  green: 'bg-green-100 text-green-700',
  red: 'bg-red-100 text-red-700',
  yellow: 'bg-yellow-100 text-yellow-700',
  gray: 'bg-gray-100 text-gray-500',
  blue: 'bg-blue-100 text-blue-700',
};

const dotColorMap = {
  green: 'bg-green-500',
  red: 'bg-red-500',
  yellow: 'bg-yellow-500',
  gray: 'bg-gray-400',
  blue: 'bg-blue-500',
};

const StatusBadge = ({ label, color = 'gray', showDot = true, className }) => {
  return (
    <span
      className={clsx(
        'inline-flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-medium',
        colorMap[color] || colorMap.gray,
        className
      )}
    >
      {showDot && (
        <span className={clsx('w-1.5 h-1.5 rounded-full', dotColorMap[color] || dotColorMap.gray)} />
      )}
      {label}
    </span>
  );
};

export default StatusBadge;
