const BOGOTA_TIME_ZONE = 'America/Bogota';
const ISO_DATE_TIME = /^\d{4}-\d{2}-\d{2}T/i;
const HAS_EXPLICIT_ZONE = /(?:Z|[+-]\d{2}:?\d{2})$/i;

/**
 * PostgreSQL/FastAPI can return a UTC timestamp without an explicit suffix.
 * Browsers otherwise interpret that value as local time, shifting the display.
 */
export const parseApiTimestamp = (value) => {
  if (!value) return null;
  if (value instanceof Date) {
    return Number.isNaN(value.getTime()) ? null : value;
  }

  const normalized = typeof value === 'string' && ISO_DATE_TIME.test(value) &&
    !HAS_EXPLICIT_ZONE.test(value)
    ? `${value}Z`
    : value;
  const date = new Date(normalized);
  return Number.isNaN(date.getTime()) ? null : date;
};

export const formatBogotaTime = (value, fallback = 'N/A') => {
  const date = parseApiTimestamp(value);
  return date
    ? date.toLocaleTimeString('es-CO', {
        hour: '2-digit',
        minute: '2-digit',
        timeZone: BOGOTA_TIME_ZONE,
      })
    : fallback;
};

export const formatBogotaDateTime = (value, options = {}, fallback = 'N/A') => {
  const date = parseApiTimestamp(value);
  return date
    ? date.toLocaleString('es-CO', {
        ...options,
        timeZone: BOGOTA_TIME_ZONE,
      })
    : fallback;
};

export const formatBogotaDate = (value, options = {}, fallback = 'N/A') => {
  const date = parseApiTimestamp(value);
  return date
    ? date.toLocaleDateString('es-CO', {
        ...options,
        timeZone: BOGOTA_TIME_ZONE,
      })
    : fallback;
};
