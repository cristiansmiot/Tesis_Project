#include "power_ctrl_driver.h"

#include "logger.h"

static const char *TAG = "power_ctrl_driver";

/**
 * @brief Inicializacion stub para sprint 5.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED mientras el modulo no este implementado.
 */
esp_err_t power_ctrl_driver_init(void)
{
    LOG_WARN(TAG, "power_ctrl_driver no implementado - Sprint 5");
    return ESP_ERR_NOT_SUPPORTED;
}

