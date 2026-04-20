# ARQUITECTURA

## Diagrama de Capas

```
+-------------------------------------------------------------+
| CAPA TASKS                                                  |
| task_measurement | task_pq | task_calibration | task_ui     |
| Usa primitivas FreeRTOS y controla actualizaciones MeterData_t |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| CAPA MODULOS                                                |
| measurements | calibration | power_quality | diagnostics    |
| Sin dependencia de FreeRTOS y sin propiedad de MeterData_t  |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| CAPA DRIVERS                                                |
| ade9153a_spi | ade9153a_init | ade9153a_hal                |
| Solo acceso a hardware, sin dependencia a modules/tasks/data |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| HARDWARE                                                     |
| ADE9153A + ESP32-S3                                          |
+-------------------------------------------------------------+
```

## Dependencias permitidas

| Desde capa    | Puede incluir/llamar |
|---            |---|
| tasks         | modules, data, drivers (via APIs publicas), utils |
| modules       | drivers, utils |
| drivers       | board/config, APIs de hardware ESP-IDF, utils |
| data          | sincronizacion FreeRTOS + utils + diagnostics |
| utils         | utilidades reutilizables autonomas |

## Convenciones de Naming

- Funciones y variables: `snake_case`
- Constantes y registros: `UPPER_CASE` con prefijo de modulo
- Tipos: sufijo `_t` (`MeterData_t`, `FaultCode_t`, `MeterEvent_t`)
- Prefijo de modulo en simbolos (`ade9153a_`, `voltage_`, `meter_data_`, `task_`)

## Resumen de Reglas Numericas y de Tipos

- Registros ADE signed deben castearse a tipo signed de C antes de convertir.
- Registros ADE unsigned se mantienen unsigned (`uint32_t`/`uint16_t`).
- Las conversiones a unidades fisicas se hacen en `float`.
- Nunca mezclar valor crudo y valor fisico en la misma variable.
- Cualquier formula o registro no verificado debe marcarse con `TODO` y quedar fuera de calculos criticos.

## Agregar nuevo modulo de medicion (5 pasos)

1. Crear `module_name.h/.c` en `modules/measurements`.
2. Agregar `module_name_init(constants...)` y getters puros sin FreeRTOS.
3. Leer registros ADE solo por APIs de `ade9153a_spi`.
4. Convertir crudo a valor fisico con cast signed/unsigned explicito y comentario de unidad.
5. Registrar el modulo en la secuencia de init de `main/main.c`.

## Integrar Nuevo Driver de Hardware (4 pasos)

1. Agregar carpeta/header/source del driver en `drivers/`.
2. Mantener acceso a hardware aislado en archivos del driver.
3. Exponer solo API publica en el header y estado interno como `static`.
4. Agregar el source en `main/CMakeLists.txt` y consumirlo solo desde modules/tasks.

## Agregar nueva tarea periodica (3 pasos)

1. Crear `task_x.h/.c` y registrar su creacion en `task_manager_start`.
2. Usar `vTaskDelayUntil()` como primer statement del loop.
3. Comunicar tareas con colas/notificaciones y timeouts explicitos.

