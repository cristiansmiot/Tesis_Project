# Sprint 4 - Cableado UI (OLED + Botones)

## Hardware minimo

- 1 pantalla OLED SSD1306 I2C de 128x64, 3.3V, direccion 0x3C.
- 3 pulsadores momentaneos normalmente abiertos.
- Cables dupont.
- Alimentacion a 3.3V y GND desde la ESP32-S3.

## Mapa de pines actual en firmware

Tomado de `config/board_config.h`:

- OLED SDA -> GPIO8
- OLED SCL -> GPIO9
- BTN_UP -> GPIO1
- BTN_SELECT -> GPIO2
- BTN_DOWN -> GPIO42

Los botones estan implementados con `pull-up` interno y logica activa en bajo.
Eso significa que cada pulsador se conecta entre el GPIO y GND.
No requiere resistencias externas para funcionar en la version actual.

## Diagrama rapido

```text
ESP32-S3 DevKitC-1

3V3  ---------------------> OLED VCC
GND  ---------------------> OLED GND
GPIO8 --------------------> OLED SDA
GPIO9 --------------------> OLED SCL

GPIO1 ----o  PUSH UP  o---- GND
GPIO2 ----o PUSH SEL  o---- GND
GPIO42 ---o PUSH DOWN o---- GND
```

## Funcion de cada boton

- BTN_UP: subir en menus o cambiar a la pantalla anterior.
- BTN_SELECT: entrar/ejecutar opcion.
- BTN_DOWN: bajar en menus o cambiar a la pantalla siguiente.
- BACK: no usa boton extra. Se genera con pulsacion larga sobre `SELECT`.

## Conveniencia de los GPIO elegidos

### Recomendacion practica

Para una primera integracion de laboratorio, **si convienen** y puedes cablearlos asi.
La principal ventaja es que `GPIO1`, `GPIO2` y `GPIO42` estan juntos en el header J3 de la DevKitC-1,
asi que el arnes queda simple y ordenado.

### Analisis por pin

- `GPIO1`: bueno para boton, no esta usado por otro modulo del proyecto.
- `GPIO2`: bueno para boton, no esta usado por otro modulo del proyecto.
- `GPIO42`: funcionalmente sirve, pero tiene una salvedad importante: comparte rol con la interfaz JTAG externa del chip.

En la practica eso significa:

- Si vas a depurar por USB normal y no vas a sacar JTAG externo por pines, `GPIO42` se puede usar sin problema para el boton.
- Si mas adelante quieres usar JTAG externo por los GPIO dedicados a esa funcion, entonces **no conviene** dejar el boton en `GPIO42`.

## GPIO que conviene evitar para botones

En esta placa y este proyecto, evita usar para botones:

- `GPIO0`, `GPIO3`, `GPIO45`, `GPIO46`: pines de strapping/arranque.
- `GPIO19`, `GPIO20`: usados por USB-Serial/JTAG integrado por defecto.
- `GPIO10` a `GPIO14`: ya usados por SPI del ADE9153A.
- `GPIO16`, `GPIO17`: ya usados por UART del SIM7080G.
- `GPIO8`, `GPIO9`: ya usados por I2C del OLED.
- `GPIO38`: ya usado por LED de estado.
- `GPIO15`: ya usado por LED de red.
- `GPIO48`: ya usado por LED de error.
- `GPIO37`, `GPIO39`, `GPIO40`: mejor reservarlos por JTAG, segun la definicion actual del proyecto.

## Si quieres dejar libre GPIO42

La mejor decision depende de si Sprint 5 usara o no los GPIO comentados en el archivo de configuracion.
Si quieres reservar `GPIO42` desde ya, una alternativa simple es mover `BTN_DOWN` a `GPIO4` o `GPIO6`,
pero eso debe decidirse junto con el plan de gestion de energia para no pisar hardware futuro.

## Recomendacion final

- Si el objetivo inmediato es poner a funcionar la pantalla y navegar menus en prototipo: deja `GPIO1`, `GPIO2`, `GPIO42`.
- Si ya sabes que vas a usar JTAG externo mas adelante: remapea desde ahora `BTN_DOWN` fuera de `GPIO42`.
