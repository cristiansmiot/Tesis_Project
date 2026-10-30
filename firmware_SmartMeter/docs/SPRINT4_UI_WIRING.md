# Sprint 4 - Cableado UI (OLED + teclado de cuatro teclas)

Fecha de actualizacion: 24 de octubre de 2026.

## Hardware instalado

- Pantalla OLED SSD1306 I2C, 128x64, 3.3 V, direccion `0x3C`.
- Teclado de membrana lineal de cuatro teclas con comun a GND.
- Cuatro resistencias externas de 10 kohm a 3.3 V en la PCB.
- ESP32-S3.

## Mapa definitivo

Tomado de `config/board_config.h` y validado por pulsacion fisica:

| Posicion / rotulo | Accion | GPIO |
|---|---|---:|
| Tecla 4, superior | UP | 2 |
| Tecla 3 | DOWN | 1 |
| Tecla 2 | SELECT | 41 |
| Tecla 1, inferior | BACK | 42 |

El comun del teclado se conecta a GND. Cada entrada se mantiene en alto con la
resistencia externa de 10 kohm y la pulsacion la lleva a nivel bajo. El driver
tambien habilita `pull-up` interno; su presencia no sustituye las resistencias
ya montadas y no requiere recablear la PCB.

## Diagrama logico

```text
3.3 V -- 10k -- GPIO2  ---- tecla 4 / UP -----+
3.3 V -- 10k -- GPIO1  ---- tecla 3 / DOWN ---+
3.3 V -- 10k -- GPIO41 ---- tecla 2 / SELECT -+--- GND comun
3.3 V -- 10k -- GPIO42 ---- tecla 1 / BACK ---+

OLED SDA ---------------- GPIO8
OLED SCL ---------------- GPIO9
OLED VCC ---------------- 3.3 V
OLED GND ---------------- GND
```

## Comportamiento del firmware

- Logica activa en bajo.
- Antirrebote y un solo evento por pulsacion corta.
- SELECT largo conserva el retorno como alternativa de navegacion.
- BACK tiene tecla fisica dedicada en GPIO42.
- La UI no debe bloquear medicion, SD ni comunicaciones.

## Evidencia al corte

Antes del remapeo final se comprobaron las cuatro lineas de entrada y se
observaron pulsaciones de teclas 1, 2, 3 y 4, incluida tecla 4 sostenida,
repetida y corta. El mapa definitivo compilo y fue cargado en la placa.

## Prueba corta pendiente

Con todos los modulos conectados y antes de la corrida de 24 h:

1. Pulsar tecla 4 una vez y comprobar un desplazamiento UP.
2. Pulsar tecla 3 una vez y comprobar un desplazamiento DOWN.
3. Pulsar tecla 2 una vez y comprobar SELECT.
4. Pulsar tecla 1 una vez y comprobar BACK.
5. Mantener SELECT y confirmar que el fallback BACK se produce una sola vez.
6. Revisar que no aparezcan resets, errores SPI ni dobles eventos.

Criterio: 5/5 acciones correctas y cero interferencias con ADE, SD, RTC o SIM.

## Precauciones

- Conectar o desconectar el teclado solo con la placa desenergizada.
- No aplicar 5 V a ninguna entrada.
- No mover las lineas SPI ni I2C para esta prueba.
