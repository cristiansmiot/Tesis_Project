#include "nvm.h"

#include "logger.h"

static const char *TAG = "nvm";

/**
 * @brief Inicializacion stub para sprint 1.2.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED mientras el modulo no este implementado.
 */
esp_err_t nvm_init(void)
{
    LOG_WARN(TAG, "nvm no implementado - Sprint 1.2");
    return ESP_ERR_NOT_SUPPORTED;
}

