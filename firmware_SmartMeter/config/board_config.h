#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "driver/i2c.h"
#include "driver/spi_common.h"

// ADE9153A SPI (SPI2_HOST - FSPI nativo ESP32-S3)
#define ADE9153A_SPI_HOST       SPI2_HOST
#define ADE9153A_PIN_MOSI       11
#define ADE9153A_PIN_SCLK       12
#define ADE9153A_PIN_MISO       13
#define ADE9153A_PIN_CS         10
#define ADE9153A_PIN_RESET      14
#define ADE9153A_PIN_IRQ        47
#define ADE9153A_PIN_ZX         (-1)
// 1 MHz bring-up (chip soporta hasta 10 MHz). Bajar a 100 kHz fue una
// regresion: con 100 kHz el link SPI se volvia poco fiable tras reset.
#define ADE9153A_SPI_CLK_HZ     1000000
#define ADE9153A_SPI_MODE       0

// LEDs
#define PIN_LED_STATUS          38
#define PIN_LED_NETWORK         15
#define PIN_LED_ERROR           48

// JTAG reservado (no usar): GPIO 37, 39, 40
// GPIO 38 compartido con LED_STATUS.

// Sprint 3 - SIM7080G UART1
#define SIM7080G_UART_TX        16
#define SIM7080G_UART_RX        17
#define SIM7080G_UART_NUM       1
// En pruebas reales el modulo responde estable a 57600.
// El init mantiene auto-probe como respaldo.
#define SIM7080G_BAUD_RATE      57600

// Sprint 4 - OLED I2C
#define OLED_SDA                8
#define OLED_SCL                9
#define OLED_I2C_ADDR           0x3C
#define OLED_I2C_PORT           I2C_NUM_0
#define OLED_WIDTH              128
#define OLED_HEIGHT             64
#define OLED_I2C_CLK_HZ         100000
#define OLED_I2C_TIMEOUT_MS     250U

// Sprint 4 - Botones navegacion
// GPIO1 y GPIO2 son entradas generales y quedan juntos en el header J3.
// GPIO42 tambien queda contiguo en el header y funciona bien como entrada,
// pero coincide con MTMS. Si en el futuro se usa JTAG externo por GPIO39-42,
// conviene mover este boton a otro GPIO libre.
#define PIN_BTN_UP              1
#define PIN_BTN_SELECT          2
#define PIN_BTN_DOWN            42
#define BUTTON_DEBOUNCE_MS      50U
#define BUTTON_LONGPRESS_MS     800U

// Sprint 5 - Power control
// #define PIN_BATT_ADC         4
// #define PIN_POWER_DETECT     5
// #define PIN_BACKUP_ENABLE    6

#endif // BOARD_CONFIG_H
