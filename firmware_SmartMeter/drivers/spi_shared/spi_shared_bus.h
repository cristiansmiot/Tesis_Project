#ifndef SPI_SHARED_BUS_H
#define SPI_SHARED_BUS_H

#include "esp_err.h"

/** Inicializa una sola vez SPI2, compartido por ADE9153A y microSD. */
esp_err_t spi_shared_bus_init(void);

#endif /* SPI_SHARED_BUS_H */
