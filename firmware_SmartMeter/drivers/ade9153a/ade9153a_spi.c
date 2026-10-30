#include "ade9153a_spi.h"

#include <stdbool.h>
#include <string.h>
#include "ade9153a_hal.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rtos_app_config.h"
#include "logger.h"
#include "spi_shared_bus.h"

static const char *TAG = "ade9153a_spi";

static spi_device_handle_t s_spi_handle;
static bool s_spi_ready;
static ADE9153A_HAL_t s_hal;
static bool s_hal_ready;
static SemaphoreHandle_t s_spi_mutex;

static inline uint16_t ade9153a_cmd_word(uint16_t addr, bool read_op)
{
    uint16_t cmd = (uint16_t)((addr << 4U) & 0xFFF0U);
    if (read_op) {
        cmd = (uint16_t)(cmd + 0x0008U);
    }
    return cmd;
}

static bool ade9153a_spi_lock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return true;
    }
    if (s_spi_mutex == NULL) {
        return true;
    }
    const TickType_t timeout_ticks = pdMS_TO_TICKS(TIMEOUT_MUTEX_MS);
    if (xSemaphoreTake(s_spi_mutex, timeout_ticks) != pdTRUE) {
        LOG_WARN(TAG, "SPI mutex timeout");
        return false;
    }
    return true;
}

static void ade9153a_spi_unlock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }
    if (s_spi_mutex != NULL) {
        (void)xSemaphoreGive(s_spi_mutex);
    }
}

/**
 * @brief Callback conservado por compatibilidad con el HAL.
 *
 * La transferencia adquiere primero SPI2 compartido y controla CS dentro de
 * esa seccion critica. Hacerlo aqui bajaria CS mientras la SD usa el bus.
 */
static void ade9153a_cs_set_impl(bool active)
{
    (void)active;
}

/**
 * @brief Activa o desactiva reset del ADE9153A.
 * @param active true para reset activo.
 */
static void ade9153a_reset_set_impl(bool active)
{
    (void)gpio_set_level(ADE9153A_PIN_RESET, active ? 0 : 1);
}

/**
 * @brief Delay de plataforma para secuencias de init.
 * @param ms Retardo en milisegundos.
 */
static void ade9153a_platform_delay_ms_impl(uint32_t ms)
{
    // Usar vTaskDelay cuando el scheduler esta activo para ceder CPU.
    // esp_rom_delay_us es busy-wait: con ms=1000 bloquea el task 1s y puede
    // disparar task WDT si se concatenan delays (reset = 300+100+1000 = 1.4s).
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        const TickType_t ticks = pdMS_TO_TICKS(ms);
        vTaskDelay((ticks > 0U) ? ticks : 1U);
    } else {
        esp_rom_delay_us((uint32_t)(ms * 1000U));
    }
}

/**
 * @brief Transfiere un buffer por SPI.
 * @param tx Buffer TX (puede ser NULL).
 * @param rx Buffer RX (puede ser NULL).
 * @param len Longitud en bytes.
 * @return ESP_OK en caso de exito.
 */
static esp_err_t ade9153a_spi_transfer_impl(uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!s_spi_ready || (s_spi_handle == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t t = {0};
    t.length = (uint32_t)(len * 8U);
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    /* CS manual es requisito de la interfaz existente del ADE. Adquirir SPI2
     * antes de bajarlo mantiene atomica la secuencia frente a SDSPI. */
    esp_err_t err = spi_device_acquire_bus(s_spi_handle, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(ADE9153A_PIN_CS, 0);
    if (err == ESP_OK) {
        /* Margen de establecimiento CS->SCLK. Antes lo aportaba de forma
         * accidental la configuracion del dispositivo dentro del driver. */
        esp_rom_delay_us(2U);
        err = spi_device_polling_transmit(s_spi_handle, &t);
        esp_rom_delay_us(2U);
    }
    const esp_err_t cs_err = gpio_set_level(ADE9153A_PIN_CS, 1);
    spi_device_release_bus(s_spi_handle);

    return (err != ESP_OK) ? err : cs_err;
}

/**
 * @brief Inicializa SPI y HAL del ADE9153A.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_spi_init(void)
{
    if (s_spi_ready) {
        return ESP_OK;
    }

    const gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << ADE9153A_PIN_CS) | (1ULL << ADE9153A_PIN_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&out_cfg);
    if (err != ESP_OK) {
        return err;
    }
    (void)gpio_set_level(ADE9153A_PIN_CS, 1);
    (void)gpio_set_level(ADE9153A_PIN_RESET, 1);

    const gpio_config_t irq_cfg = {
        .pin_bit_mask = (1ULL << ADE9153A_PIN_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    err = gpio_config(&irq_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = spi_shared_bus_init();
    if (err != ESP_OK) {
        return err;
    }

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = ADE9153A_SPI_CLK_HZ,
        .mode = ADE9153A_SPI_MODE,
        // CS manual, accionado solo despues de adquirir SPI2 compartido.
        .spics_io_num = -1,
        .queue_size = 1
    };

    err = spi_bus_add_device(ADE9153A_SPI_HOST, &dev_cfg, &s_spi_handle);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }
    if (s_spi_handle == NULL) {
        return ESP_FAIL;
    }

    ADE9153A_HAL_t hal = {
        .spi_transfer = ade9153a_spi_transfer_impl,
        .cs_set = ade9153a_cs_set_impl,
        .reset_set = ade9153a_reset_set_impl,
        .platform_delay_ms = ade9153a_platform_delay_ms_impl
    };
    err = ade9153a_hal_init(&hal);
    if (err != ESP_OK) {
        return err;
    }

    if (s_spi_mutex == NULL) {
        s_spi_mutex = xSemaphoreCreateMutex();
        if (s_spi_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_spi_ready = true;
    LOG_INFO(TAG, "SPI initialized @ %d Hz", ADE9153A_SPI_CLK_HZ);
    return ESP_OK;
}

esp_err_t ade9153a_spi_recover_bus(void)
{
    bool locked = false;
    if (s_spi_ready) {
        locked = ade9153a_spi_lock();
        if (!locked) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (s_spi_handle != NULL) {
        (void)spi_bus_remove_device(s_spi_handle);
        s_spi_handle = NULL;
    }
    /* No liberar SPI2: la microSD puede seguir registrada. Volver a agregar
     * solamente el dispositivo ADE evita interrumpir el almacenamiento. */
    s_spi_ready = false;
    s_hal_ready = false;
    (void)gpio_set_level(ADE9153A_PIN_CS, 1);
    (void)gpio_set_level(ADE9153A_PIN_RESET, 1);
    esp_rom_delay_us(1000U);

    if (locked) {
        ade9153a_spi_unlock();
    }

    LOG_WARN(TAG, "ADE SPI device recovery requested on shared bus");
    return ade9153a_spi_init();
}

/**
 * @brief Registra la capa HAL activa.
 * @param hal Tabla de callbacks HAL.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_hal_init(ADE9153A_HAL_t *hal)
{
    if ((hal == NULL) || (hal->spi_transfer == NULL) || (hal->cs_set == NULL) ||
        (hal->reset_set == NULL) || (hal->platform_delay_ms == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_hal = *hal;
    s_hal_ready = true;
    return ESP_OK;
}

/**
 * @brief Retorna el HAL activo.
 * @return Puntero al HAL o NULL si no hay HAL.
 */
const ADE9153A_HAL_t *ade9153a_hal_get(void)
{
    if (!s_hal_ready) {
        return NULL;
    }
    return &s_hal;
}

/**
 * @brief Escribe un registro de 16 bits ADE9153A.
 * @param addr Direccion de registro.
 * @param value Valor a escribir.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_write_reg16(uint16_t addr, uint16_t value)
{
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ade9153a_spi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    const uint16_t cmd = ade9153a_cmd_word(addr, false);
    uint8_t tx[4];
    tx[0] = (uint8_t)(cmd >> 8U);
    tx[1] = (uint8_t)(cmd & 0x00FFU);
    tx[2] = (uint8_t)(value >> 8U);
    tx[3] = (uint8_t)(value & 0x00FFU);

    hal->cs_set(true);
    const esp_err_t err = hal->spi_transfer(tx, NULL, sizeof(tx));
    hal->cs_set(false);
    ade9153a_spi_unlock();
    return err;
}

/**
 * @brief Escribe un registro de 32 bits ADE9153A.
 * @param addr Direccion de registro.
 * @param value Valor a escribir.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_write_reg32(uint16_t addr, uint32_t value)
{
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ade9153a_spi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    const uint16_t cmd = ade9153a_cmd_word(addr, false);
    uint8_t tx[6];
    tx[0] = (uint8_t)(cmd >> 8U);
    tx[1] = (uint8_t)(cmd & 0x00FFU);
    tx[2] = (uint8_t)(value >> 24U);
    tx[3] = (uint8_t)((value >> 16U) & 0xFFU);
    tx[4] = (uint8_t)((value >> 8U) & 0xFFU);
    tx[5] = (uint8_t)(value & 0xFFU);

    hal->cs_set(true);
    const esp_err_t err = hal->spi_transfer(tx, NULL, sizeof(tx));
    hal->cs_set(false);
    ade9153a_spi_unlock();
    return err;
}

/**
 * @brief Lee un registro de 16 bits ADE9153A.
 * @param addr Direccion de registro.
 * @param value Salida de lectura.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_read_reg16(uint16_t addr, uint16_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ade9153a_spi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    const uint16_t cmd = ade9153a_cmd_word(addr, true);
    uint8_t tx[4] = {0};
    uint8_t rx[4] = {0};
    tx[0] = (uint8_t)(cmd >> 8U);
    tx[1] = (uint8_t)(cmd & 0x00FFU);

    hal->cs_set(true);
    const esp_err_t err = hal->spi_transfer(tx, rx, sizeof(tx));
    hal->cs_set(false);
    if (err != ESP_OK) {
        ade9153a_spi_unlock();
        return err;
    }

    *value = (uint16_t)(((uint16_t)rx[2] << 8U) | (uint16_t)rx[3]);
    ade9153a_spi_unlock();
    return ESP_OK;
}

/**
 * @brief Lee un registro de 32 bits ADE9153A.
 * @param addr Direccion de registro.
 * @param value Salida de lectura.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_read_reg32(uint16_t addr, uint32_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ade9153a_spi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    const uint16_t cmd = ade9153a_cmd_word(addr, true);
    uint8_t tx[6] = {0};
    uint8_t rx[6] = {0};
    tx[0] = (uint8_t)(cmd >> 8U);
    tx[1] = (uint8_t)(cmd & 0x00FFU);

    hal->cs_set(true);
    const esp_err_t err = hal->spi_transfer(tx, rx, sizeof(tx));
    hal->cs_set(false);
    if (err != ESP_OK) {
        ade9153a_spi_unlock();
        return err;
    }

    *value = ((uint32_t)rx[2] << 24U) |
             ((uint32_t)rx[3] << 16U) |
             ((uint32_t)rx[4] << 8U) |
             (uint32_t)rx[5];
    ade9153a_spi_unlock();
    return ESP_OK;
}

/**
 * @brief Lee un registro de 8 bits ADE9153A.
 * @param addr Direccion de registro.
 * @param value Salida de lectura.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_read_reg8(uint16_t addr, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw16 = 0U;
    const esp_err_t err = ade9153a_read_reg16(addr, &raw16);
    if (err != ESP_OK) {
        return err;
    }

    *value = (uint8_t)(raw16 & 0x00FFU);
    return ESP_OK;
}

