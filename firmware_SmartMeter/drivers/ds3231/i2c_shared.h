#ifndef I2C_SHARED_H
#define I2C_SHARED_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Inicializa una sola vez el bus I2C usado por OLED y DS3231. */
esp_err_t i2c_shared_init(void);

/** Serializa una transaccion completa sobre el bus compartido. */
esp_err_t i2c_shared_lock(TickType_t wait_ticks);

/** Libera el bus despues de una llamada exitosa a i2c_shared_lock(). */
void i2c_shared_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_SHARED_H */
