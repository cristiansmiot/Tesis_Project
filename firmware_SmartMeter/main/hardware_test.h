#ifndef HARDWARE_TEST_H
#define HARDWARE_TEST_H

#include <stdbool.h>

// Perfiles seleccionables desde platformio.ini mediante build_flags.
#define HW_TEST_PROFILE_NONE          0
#define HW_TEST_PROFILE_OLED          1
#define HW_TEST_PROFILE_RTC           2
#define HW_TEST_PROFILE_SD            3
#define HW_TEST_PROFILE_SIM_AT        4
#define HW_TEST_PROFILE_SIM_NETWORK   5
#define HW_TEST_PROFILE_ALL_NO_ADE    6
#define HW_TEST_PROFILE_SIM_PWRKEY    7
#define HW_TEST_PROFILE_OLED_RTC      8
#define HW_TEST_PROFILE_OLED_RTC_SD   9
#define HW_TEST_PROFILE_SYSTEM_CONTRACTS 10
#define HW_TEST_PROFILE_RTC_SERVICE   11
#define HW_TEST_PROFILE_OLED_RTC_SERVICE 12
#define HW_TEST_PROFILE_SD_SERVICE    13
#define HW_TEST_PROFILE_OLED_RTC_SD_SERVICE 14

#ifndef METER_HW_TEST_PROFILE
#define METER_HW_TEST_PROFILE HW_TEST_PROFILE_NONE
#endif

#if (METER_HW_TEST_PROFILE < HW_TEST_PROFILE_NONE) || \
    (METER_HW_TEST_PROFILE > HW_TEST_PROFILE_OLED_RTC_SD_SERVICE)
#error "METER_HW_TEST_PROFILE fuera de rango"
#endif

/** Retorna true para los perfiles que reemplazan el arranque normal. */
bool hardware_test_is_standalone(void);

/** Nombre corto del perfil compilado. */
const char *hardware_test_profile_name(void);

/** Ejecuta el perfil standalone seleccionado. No retorna. */
void hardware_test_run(void);

#endif // HARDWARE_TEST_H
