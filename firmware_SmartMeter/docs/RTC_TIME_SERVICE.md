# Servicio RTC y tiempo del sistema

## Objetivo

El DS3231 mantiene la hora entre apagados y el ESP32 trabaja internamente en
UTC. La presentación en OLED y los archivos legibles usan Bogotá (`-05:00`).
Una fecha fuera de 2020-01-01 a 2099-12-31 nunca se acepta ni se inventa.

## Arquitectura

- `i2c_shared`: instala una sola vez el bus de GPIO8/GPIO9 y serializa OLED y
  RTC con un mutex estático.
- `ds3231`: valida BCD, calendario, modo 12/24 horas, rango y bandera OSF.
- `time_service`: conserva el snapshot común, aplica sincronizaciones y
  formatea UTC/Bogotá.
- `task_time`: es el único propietario del ciclo de vida del RTC y ejecuta la
  recuperación 1, 2 y 4 segundos; después sondea cada 60 segundos.
- `task_communication`: valida `AT+CCLK` y entrega el epoch al servicio; no
  escribe directamente el reloj ni modifica el TZ global.
- `task_ui`: solo consulta snapshots; nunca consume eventos.

## Prioridad de fuentes

1. Al arrancar, una fecha RTC válida inicializa el reloj del ESP32.
2. Una fecha celular o remota válida actualiza el reloj del ESP32 y el DS3231.
3. Si la red no ofrece fecha, el RTC continúa siendo la fuente disponible.
4. Si ninguna fuente es válida se conserva `timestamp=0`, `time_valid=false`,
   uptime, `boot_id` y secuencia.

El firmware reintenta hora celular cada 60 segundos mientras sea inválida y
renueva la sincronización cada 6 horas cuando ya es válida.

## OSF y escritura segura

La bandera Oscillator Stop Flag no se borra durante `init`. Solo se limpia al
escribir una hora válida, releerla y confirmar que coincide. Esto permite
distinguir un RTC presente de un RTC cuya hora no es confiable.

## Aislamiento de fallas

Una falla I2C del RTC actualiza su snapshot y genera eventos comunes, pero no
reinicia el ESP32 ni bloquea OLED, SD o comunicaciones. El módulo pasa de
`DEGRADED` a `RECOVERING` y, tras agotar los intentos rápidos, a `FAILED` con
sondeo lento. Una lectura válida posterior lo devuelve a `OK` y genera
`CLEARED`.

## Perfiles de validación

- `test_rtc_service`: tarea y servicio reales, sin pantalla.
- `test_oled_rtc_service`: tarea real más visualización y bus compartido.
- `test_system_contracts`: conversión calendario UTC, fecha bisiesta inválida,
  eventos, concurrencia y prioridad de cola.

La validación física termina con tres evidencias: avance continuo, desconexión
y recuperación del RTC, y retención de hora durante 5 a 10 minutos sin
alimentación principal.
