# Ensayo TCP R2 del SIM7080G — 29 de octubre de 2026

## Propósito

Comprobar si el módem puede abrir un socket TCP al proxy MQTT de Railway sin enviar publicaciones MQTT. El equipo se alimentó por USB y permaneció sin AC.

## Resultado observado

Se cargó el perfil `1.1.0-tcpdiag-r2` en COM6. En cuatro intentos capturados, el comando `AT+CAOPEN` fue rechazado rápidamente con `+CME ERROR: operation not allowed` (aprox. 321 ms). Inmediatamente después, el módem informó:

- `CEREG: 0,5`: registrado en roaming.
- `CNACT: 0,1` con dirección `10.66.42.214`: contexto PDP activo.
- `CPSI`: LTE CAT-M1 en línea.
- `CASTATE?`: no listó una conexión abierta.
- `SMSTATE: 0`: cliente MQTT desconectado.
- `CAOPEN=?`: `(0-12),(0-4),("TCP","UDP","NONIP"),64,(1-65535)`; el equipo no anunció un parámetro `recv_mode`.

## Interpretación

R2 enviaba `AT+CAOPEN=0,0,"TCP","shinkansen.proxy.rlwy.net",58954,0`, es decir, incluía seis parámetros. La respuesta del equipo enumeró cinco campos y omitió el opcional `recv_mode`. Aunque el manual general SIMCom V1.07 documenta ese parámetro opcional, la prueba de capacidades ejecutada en este módem no lo anunció. Por lo tanto, los rechazos de R2 no se deben usar como evidencia de pérdida de cobertura, falla del operador ni indisponibilidad del proxy: la prueba no llegó a confirmar que el intento TCP se iniciara.

## Decisión

Se corrigió el perfil R3 para omitir el sexto parámetro. Se volverá a evaluar la apertura antes de iniciar una corrida prolongada. El log serial se conserva en `evidence/sim-tcpdiag-r2-20261029.log`; el binario y las fuentes exactas están en `evidence/versions/2026-10-29_tcpdiag-r2-build/`.
