#ifndef ADE9153A_SPI_H
#define ADE9153A_SPI_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Inicializa SPI y pines de control para ADE9153A.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_spi_init(void);

/**
 * @brief Rehace la capa SPI del ADE9153A (bus + device + HAL).
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_spi_recover_bus(void);

/**
 * @brief Lee un registro de 32 bits.
 * @param addr Direccion del registro.
 * @param value Salida de lectura.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_read_reg32(uint16_t addr, uint32_t *value);

/**
 * @brief Escribe un registro de 32 bits.
 * @param addr Direccion del registro.
 * @param value Valor a escribir.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_write_reg32(uint16_t addr, uint32_t value);

/**
 * @brief Lee un registro de 16 bits.
 * @param addr Direccion del registro.
 * @param value Salida de lectura.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_read_reg16(uint16_t addr, uint16_t *value);

/**
 * @brief Escribe un registro de 16 bits.
 * @param addr Direccion del registro.
 * @param value Valor a escribir.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_write_reg16(uint16_t addr, uint16_t value);

/**
 * @brief Lee un registro de 8 bits.
 * @param addr Direccion del registro.
 * @param value Salida de lectura.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_read_reg8(uint16_t addr, uint8_t *value);

#endif // ADE9153A_SPI_H
