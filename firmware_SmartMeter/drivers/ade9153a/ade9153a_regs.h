#ifndef ADE9153A_REGS_H
#define ADE9153A_REGS_H

/*
 * Direcciones verificadas contra:
 * - ADE9153A.h (Analog Devices Arduino library, auto-generated register map)
 * - ADE9153A Datasheet Rev.0 (register summary)
 */

// DSP output / metrology
#define ADE9153A_REG_AIRMS             0x0202  // 32-bit U, Current Ch A RMS
#define ADE9153A_REG_AVRMS             0x0203  // 32-bit U, Voltage RMS
#define ADE9153A_REG_AWATT             0x0204  // 32-bit S, Active power
#define ADE9153A_REG_AVA               0x0206  // 32-bit U, Apparent power
#define ADE9153A_REG_AFVAR             0x0207  // 32-bit S, Reactive power
#define ADE9153A_REG_APF               0x0208  // 32-bit S, PF in 5.27 format
#define ADE9153A_REG_BIRMS             0x0212  // 32-bit U, Current Ch B RMS

// Frequency / period
#define ADE9153A_REG_APERIOD           0x0418  // 32-bit U, Line period

// Energy accumulation
#define ADE9153A_REG_AWATTHR_LO        0x039E  // 32-bit U, Active energy LSBs
#define ADE9153A_REG_AWATTHR_HI        0x039F  // 32-bit S, Active energy MSBs
#define ADE9153A_REG_AVAHR_LO          0x03B2  // 32-bit U, Apparent energy LSBs
#define ADE9153A_REG_AFVARHR_HI        0x03BD  // 32-bit S, Reactive energy MSBs
#define ADE9153A_REG_AVAHR_HI          0x03B3  // 32-bit U, Apparent energy MSBs
#define ADE9153A_REG_AFVARHR_LO        0x03BC  // 32-bit U, Reactive energy LSBs

// Gain and calibration
#define ADE9153A_REG_AIGAIN            0x0000  // 32-bit S, Current A gain
#define ADE9153A_REG_APHASECAL         0x0001  // 32-bit S, Phase calibration
#define ADE9153A_REG_AVGAIN            0x0002  // 32-bit S, Voltage gain
#define ADE9153A_REG_APGAIN            0x0005  // 32-bit S, Active power gain
#define ADE9153A_REG_BIGAIN            0x0010  // 32-bit S, Current B gain
#define ADE9153A_REG_CONFIG0           0x0020  // 32-bit U, DSP config
#define ADE9153A_REG_BI_PGAGAIN        0x0023  // 16-bit U, PGA gain Ch B
#define ADE9153A_REG_CT_PHASE_DELAY    0x0049  // 16-bit S, CT phase delay
#define ADE9153A_REG_CT_CORNER         0x004A  // 16-bit U, CT corner

// Configuration
#define ADE9153A_REG_VDIV_RSMALL       0x004C  // 32-bit U, RSMALL value
#define ADE9153A_REG_MASK              0x0405  // 32-bit U, IRQ mask
#define ADE9153A_REG_VLEVEL            0x040F  // 32-bit U, Reactive power setup
#define ADE9153A_REG_ACT_NL_LVL        0x041C  // 32-bit U, No-load active threshold
#define ADE9153A_REG_REACT_NL_LVL      0x041D  // 32-bit U, No-load reactive threshold
#define ADE9153A_REG_APP_NL_LVL        0x041E  // 32-bit U, No-load apparent threshold
#define ADE9153A_REG_WTHR              0x0420  // 32-bit U, Active threshold
#define ADE9153A_REG_VARTHR            0x0421  // 32-bit U, Reactive threshold
#define ADE9153A_REG_VATHR             0x0422  // 32-bit U, Apparent threshold
#define ADE9153A_REG_RUN               0x0480  // 16-bit U, DSP run control
#define ADE9153A_REG_CONFIG1           0x0481  // 16-bit U, Config1
#define ADE9153A_REG_CFMODE            0x0490  // 16-bit U, CF mode
#define ADE9153A_REG_COMPMODE          0x0491  // 16-bit U, Computation mode
#define ADE9153A_REG_ACCMODE           0x0492  // 16-bit U, Accumulation mode
#define ADE9153A_REG_CONFIG3           0x0493  // 16-bit U, Config3
#define ADE9153A_REG_CF1DEN            0x0494  // 16-bit U, CF1 denominator
#define ADE9153A_REG_CF2DEN            0x0495  // 16-bit U, CF2 denominator
#define ADE9153A_REG_ZX_CFG            0x049A  // 16-bit U, ZX config
#define ADE9153A_REG_TEMP_TRIM         0x0471  // 32-bit U, Temp gain/offset trim (DS/ADI API)
#define ADE9153A_REG_CHIP_ID_HI        0x0472  // 32-bit U, Chip ID high word (DS/ADI API)
#define ADE9153A_REG_CHIP_ID_LO        0x0473  // 32-bit U, Chip ID low word (DS/ADI API)
#define ADE9153A_REG_CONFIG2           0x04AF  // 16-bit U, Config2
#define ADE9153A_REG_EP_CFG            0x04B0  // 16-bit U, Energy config
#define ADE9153A_REG_EGY_TIME          0x04B2  // 16-bit U, Energy update period
#define ADE9153A_REG_TEMP_CFG          0x04B6  // 16-bit U, Temp config
#define ADE9153A_REG_TEMP_RSLT         0x04B7  // 16-bit S, Internal temp result
#define ADE9153A_REG_AI_PGAGAIN        0x04B9  // 16-bit U, PGA gain Ch A
#define ADE9153A_REG_VERSION           0x04FE  // 16-bit U, Product version
#define ADE9153A_REG_VERSION_PRODUCT   0x0242  // 32-bit U, Product signature

// mSure (Sprint 1.2)
#define ADE9153A_REG_MS_ACAL_CFG       0x0030  // 32-bit U, mSure control
#define ADE9153A_REG_MS_ACAL_AICC      0x0220  // 32-bit S, 21.11 nA/code
#define ADE9153A_REG_MS_ACAL_AICERT    0x0221  // 32-bit S, certainty ppm
#define ADE9153A_REG_MS_ACAL_BICC      0x0222  // 32-bit S, 21.11 nA/code
#define ADE9153A_REG_MS_ACAL_BICERT    0x0223  // 32-bit S, certainty ppm
#define ADE9153A_REG_MS_ACAL_AVCC      0x0224  // 32-bit S, 21.11 nV/code
#define ADE9153A_REG_MS_ACAL_AVCERT    0x0225  // 32-bit S, certainty ppm
#define ADE9153A_REG_MS_STATUS_CURRENT 0x0240  // 32-bit U, mSure status bits

// Bit masks and constants
#define ADE9153A_BIT_SELFREQ           (1U << 4)   // ACCMODE bit4: 0=50Hz, 1=60Hz
#define ADE9153A_RUN_ON                0x0001U
#define ADE9153A_RUN_OFF               0x0000U
#define ADE9153A_PRODUCT_ID            0x0009153AU

// Valores de inicio/parada mSure (desde ADE9153AAPI.cpp)
#define ADE9153A_MSURE_START_A         0x00000013U
#define ADE9153A_MSURE_START_A_TURBO   0x00000017U
#define ADE9153A_MSURE_START_B         0x00000023U
#define ADE9153A_MSURE_START_V         0x00000043U
#define ADE9153A_MSURE_STOP            0x00000000U

#endif // ADE9153A_REGS_H
