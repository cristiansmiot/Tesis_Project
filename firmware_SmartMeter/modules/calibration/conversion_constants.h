#ifndef CONVERSION_CONSTANTS_H
#define CONVERSION_CONSTANTS_H

#include "meter_config.h"

// Usar constantes oficiales de ADE9153AAPI.h (EV-ADE9153ASHIELDZ).
// Fuente verificada:
// docs/references/adi_arduino/Arduino Uno R3/libraries/ADE9153A/ADE9153AAPI.h
//   CAL_IRMS_CC   = 0.838190  [uA/code]
//   CAL_VRMS_CC   = 13.41105  [uV/code]
//   CAL_POWER_CC  = 1508.743  [uW/code]
//   CAL_ENERGY_CC = 0.858307  [uWh/code]
#define METER_USE_ADI_API_CC_DEFAULTS 1

// Flags de verificacion de conversiones (Regla TN-5)
// 0 = NO verificado -> el modulo consumidor debe retornar 0.0f + LOG_WARN.
// 1 = verificado contra fuente oficial.
#define METER_AIHEADROOM_VERIFIED    1
#define METER_VHEADROOM_VERIFIED     0
#define METER_ENERGY_REGS_VERIFIED   1
#define METER_APF_FORMAT_VERIFIED    1
#define METER_TEMP_FORMULA_VERIFIED  0

// Constantes oficiales ADI API para EV board.
#define ADE9153A_CAL_IRMS_CC_UA_PER_CODE     0.838190f
#define ADE9153A_CAL_VRMS_CC_UV_PER_CODE     13.41105f
#define ADE9153A_CAL_POWER_CC_UW_PER_CODE    1508.743f
#define ADE9153A_CAL_ENERGY_CC_UWH_PER_CODE  0.858307f

// Paso 1: AIHEADROOM - AN-1571 Eq.1
// AIHEADROOM = 1V / (AI_PGAGAIN * RSHUNT_OHM * IMAX * sqrt(2))
#define METER_AIHEADROOM          4.41942f

// Paso 2: VHEADROOM - AN-1571 (voltage channel)
// VHEADROOM = (0.5V / VNOM) * ((RBIG + RSMALL) / RSMALL)
#define METER_VHEADROOM           1.47461f

// Paso 3: Constantes de conversion para modulos de medicion.
#if METER_USE_ADI_API_CC_DEFAULTS
// Modo Sprint 1 EV board: usar CC oficiales de ADE9153AAPI.
// TARGET_AICC [nA/code] = CAL_IRMS_CC[uA/code] * 1000
#define TARGET_AICC   (ADE9153A_CAL_IRMS_CC_UA_PER_CODE * 1000.0f)
// TARGET_AVCC [nV/code] = CAL_VRMS_CC[uV/code] * 1000
#define TARGET_AVCC   (ADE9153A_CAL_VRMS_CC_UV_PER_CODE * 1000.0f)
// TARGET_WCC [uW/code] = CAL_POWER_CC[uW/code]
#define TARGET_WCC    (ADE9153A_CAL_POWER_CC_UW_PER_CODE)
// TARGET_WHCC [nWh/code] = CAL_ENERGY_CC[uWh/code] * 1000
#define TARGET_WHCC   (ADE9153A_CAL_ENERGY_CC_UWH_PER_CODE * 1000.0f)
#else
// Ruta AN-1571 (queda para Sprint 1.2 con calibracion/mSure completa).
// TARGET_AICC [nA/code]
#define TARGET_AICC   ((METER_IMAX_A) * (METER_AIHEADROOM) * 52725703.0f)
// TARGET_AVCC [nV/code]
#define TARGET_AVCC   ((METER_VNOM_V) * (METER_VHEADROOM) * 26362852.0f)
// TARGET_WCC [uW/code]
#define TARGET_WCC    ((TARGET_AICC) * (TARGET_AVCC) / (float)(1U << 27))
// TARGET_WHCC [nWh/code]
#define TARGET_WHCC   ((TARGET_WCC) * (float)(1U << 13) / (3600.0f * 4000.0f))
#endif

#endif // CONVERSION_CONSTANTS_H
