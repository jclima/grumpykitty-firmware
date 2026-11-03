#include "AXP173.hpp"
#include <cmath>  // For log() function
#include <esp_sleep.h>  // For light/deep sleep functions

/* Private functions */
inline uint16_t AXP173_Class::_getMin(uint16_t a, uint16_t b) {
    return ((a)<(b)?(a):(b));
}

inline uint16_t AXP173_Class::_getMax(uint16_t a, uint16_t b) {
    return ((a)>(b)?(a):(b));
}

uint16_t AXP173_Class::_getMid(uint16_t input, uint16_t min, uint16_t max) {
    return _getMax(_getMin(input, max), min);
}

/* Locked, checked read-modify-write: new = (old & ~clearMask) | setBits.
 * Does nothing (returns false) if the read fails, so a bus error can never
 * write a garbage value back into a PMU register. */
bool AXP173_Class::_rmw(uint8_t reg, uint8_t clearMask, uint8_t setBits) {
    I2C_Class::LockGuard g(_i2c);
    uint8_t v = 0;
    if (!readRegister(reg, &v, 1)) return false;
    v = (uint8_t)((v & ~clearMask) | setBits);
    return writeRegister8(reg, v);
}

/* Public functions */
/**
 * @brief AXP173_Class init
 * 
 * @return true Init successful (device found)
 * @return false Init failed (device not found)
 */
bool AXP173_Class::begin(void) {
    // Use REG 0x12 (output enable) for detection instead of REG 0x00.
    // REG 0x00 is the power-input status register — on pure battery boot
    // (no ACIN, no VBUS, discharging) it reads 0x00, which was wrongly
    // treated as "device not present".
    // REG 0x12 POR default is 0x03 (DCDC1+LDO4), never legitimately 0x00.
    uint8_t test = readRegister8(0x12);
    _init = (test != 0xFF);
    return _init;
}

bool AXP173_Class::isACINExist() const {
    return (readRegister8(0x00) & 0B10000000) ? true : false;
}

bool AXP173_Class::isACINAvl() const {
    return (readRegister8(0x00) & 0B01000000) ? true : false;
}

bool AXP173_Class::isVBUSExist() const {
    return (readRegister8(0x00) & 0B00100000) ? true : false;
}

bool AXP173_Class::isVBUSAvl() const {
    return (readRegister8(0x00) & 0B00010000) ? true : false;
}

/**
 * @brief Get bat current direction
 * 
 * @return true Bat charging
 * @return false Bat discharging
 */
bool AXP173_Class::getBatCurrentDir() const {
    return (readRegister8(0x00) & 0B00000100) ? true : false;
}

bool AXP173_Class::isAXP173OverTemp() const {
    return (readRegister8(0x01) & 0B10000000) ? true : false;
}

/**
 * @brief Get bat charging state
 * 
 * @return true Charging
 * @return false Charge finished or not charging
 */
bool AXP173_Class::isCharging() const {
    return (readRegister8(0x01) & 0B01000000) ? true : false;
}

bool AXP173_Class::isBatExist() const {
    return (readRegister8(0x01) & 0B00100000) ? true : false;
}

bool AXP173_Class::isChargeCsmaller() const {
    return (readRegister8(0x01) & 0B00000100) ? true : false;
}

/**
 * @brief Set channels' output enable or disable
 * 
 * @param channel Output channel
 * @param state true:Enable, false:Disable
 */
void AXP173_Class::setOutputEnable(OUTPUT_CHANNEL channel, bool state) {
    _rmw(0x12, state ? 0 : (uint8_t)(1U << channel), state ? (uint8_t)(1U << channel) : 0);
}

/**
 * @brief Set channels' output voltage
 * 
 * @param channel Output channel
 * @param voltage DCDC1 & LDO4: 700~3500(mV), DCDC2: 700~2275(mV), LDO2 & LDO3: 1800~3300{mV}
 */
void AXP173_Class::setOutputVoltage(OUTPUT_CHANNEL channel, uint16_t voltage) {
    switch (channel) {
        case OP_DCDC1:
            voltage = (_getMid(voltage, 700, 3500) - 700) / 25;
            _rmw(0x26, 0B01111111, voltage & 0B01111111);
            break;
        case OP_DCDC2:
            voltage = (_getMid(voltage, 700, 2275) - 700) / 25;
            _rmw(0x23, 0B00111111, voltage & 0B00111111);
            break;
        case OP_LDO2:
            voltage = (_getMid(voltage, 1800, 3300) - 1800) / 100;
            _rmw(0x28, 0B11110000, (uint8_t)(voltage << 4));
            break;
        case OP_LDO3:
            voltage = (_getMid(voltage, 1800, 3300) - 1800) / 100;
            _rmw(0x28, 0B00001111, voltage & 0B00001111);
            break;
        case OP_LDO4:
            voltage = (_getMid(voltage, 700, 3500) - 700) / 25;
            _rmw(0x27, 0B01111111, voltage & 0B01111111);
            break;
        default:
            break;
    }
}

void AXP173_Class::powerOFF() {
    /* Best effort: retry the checked RMW; if the bus keeps failing, still
     * force the shutdown bit (other REG32 bits are non-critical). */
    for (int i = 0; i < 3; i++) {
        if (_rmw(0x32, 0, 0B10000000)) return;
    }
    writeRegister8(0x32, 0B10000000);
}

/**
 * @brief Set PWROK delay time after power good
 * 
 * @param delay_ms Delay options: 0=64ms, 1=128ms(default), 2=256ms, 3=512ms
 */
void AXP173_Class::setPWROKDelay(uint8_t delay_ms) {
    if (delay_ms > 3) delay_ms = 1;
    _rmw(0x36, 0B00000011, delay_ms);
}

/**
 * @brief Set VOFF shutdown voltage
 * 
 * @param voltage Shutdown voltage in mV (2600-3300mV), step 100mV
 */
void AXP173_Class::setVoffVoltage(uint16_t voltage) {
    voltage = _getMid(voltage, 2600, 3300);
    uint8_t value = (voltage - 2600) / 100;
    _rmw(0x31, 0B00000111, value & 0B00000111);
}

/**
 * @brief Set charge enable or disable
 * 
 * @param state true:Enable, false:Disable
 */
void AXP173_Class::setChargeEnable(bool state) {
    _rmw(0x33, state ? 0 : 0B10000000, state ? 0B10000000 : 0);
}

void AXP173_Class::setChargeCurrent(CHARGE_CURRENT current) {
    _rmw(0x33, 0B00001111, (uint8_t)current & 0B00001111);
}

/**
 * @brief Set ADC channel enable or disable
 * 
 * @param channel ADC channel
 * @param state true:Enable, false:Disable
 */
void AXP173_Class::setADCEnable(ADC_CHANNEL channel, bool state) {
    _rmw(0x82, state ? 0 : (uint8_t)(1U << channel), state ? (uint8_t)(1U << channel) : 0);
}

void AXP173_Class::setCoulometer(COULOMETER_CTRL option, bool state) {
    _rmw(0xB8, state ? 0 : (uint8_t)(1U << option), state ? (uint8_t)(1U << option) : 0);
}

uint32_t AXP173_Class::getCoulometerChargeData() const {
    uint8_t data[4] = {0, 0, 0, 0};
    readRegister(0xB0, data, 4);
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | 
           ((uint32_t)data[2] << 8) | data[3];
}

uint32_t AXP173_Class::getCoulometerDischargeData() const {
    uint8_t data[4] = {0, 0, 0, 0};
    readRegister(0xB4, data, 4);
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | 
           ((uint32_t)data[2] << 8) | data[3];
}

float AXP173_Class::GetBatCoulombInput() const {
    uint32_t coin = getCoulometerChargeData();
    // 65536 * current_LSB(0.5mA) * coin / 3600 / ADC_rate(25Hz)
    return 65536 * 0.5 * coin / 3600.0 / 25.0;
}

float AXP173_Class::GetBatCoulombOutput() const {
    uint32_t coout = getCoulometerDischargeData();
    // 65536 * current_LSB(0.5mA) * coout / 3600 / ADC_rate(25Hz)
    return 65536 * 0.5 * coout / 3600.0 / 25.0;
}

float AXP173_Class::getCoulometerData() const {
    uint32_t coin = getCoulometerChargeData();
    uint32_t coout = getCoulometerDischargeData();
    // data = 65536 * current_LSB * (coin - coout) / 3600 / ADC rate
    return 65536 * 0.5 * (int32_t)(coin - coout) / 3600.0 / 25.0;
}

float AXP173_Class::getBatVoltage() const {
    const float ADCLSB = 1.1 / 1000.0;
    uint8_t data[2] = {0, 0};
    if (!readRegister(0x78, data, 2)) return NAN;   // I2C failure: invalid sample
    uint16_t value = (data[0] << 4) | (data[1] & 0x0F);
    return value * ADCLSB;
}

float AXP173_Class::getBatCurrent() const {
    const float ADCLSB = 0.5;
    uint8_t data[2] = {0, 0};
    
    // Read charging current
    if (!readRegister(0x7A, data, 2)) return NAN;
    uint16_t CurrentIn = (data[0] << 5) | (data[1] & 0x1F);
    
    // Read discharging current
    if (!readRegister(0x7C, data, 2)) return NAN;
    uint16_t CurrentOut = (data[0] << 5) | (data[1] & 0x1F);
    
    return (CurrentIn - CurrentOut) * ADCLSB;
}

float AXP173_Class::getBatLevel() const {
    const float batVoltage = getBatVoltage();
    if (std::isnan(batVoltage)) return NAN;   // invalid sample: caller must ignore
    // 改进的电池电量计算算法 (基于典型锂电池放电曲线)
    if (batVoltage < 3.2) return 0.0;
    if (batVoltage > 4.2) return 100.0;
    // 使用分段线性插值获得更准确的电量估算
    if (batVoltage >= 4.1) return 90.0 + (batVoltage - 4.1) * 100.0;
    if (batVoltage >= 3.9) return 70.0 + (batVoltage - 3.9) * 100.0;
    if (batVoltage >= 3.7) return 40.0 + (batVoltage - 3.7) * 150.0;
    if (batVoltage >= 3.5) return 20.0 + (batVoltage - 3.5) * 100.0;
    return (batVoltage - 3.2) * 66.67;  // 3.2V~3.5V: 0%~20%
}

float AXP173_Class::getBatPower() const {
    const float VoltageLSB = 1.1;
    const float CurrentLCS = 0.5;
    uint8_t data[3] = {0, 0, 0};
    if (!readRegister(0x70, data, 3)) return NAN;
    uint32_t ReData = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    return VoltageLSB * CurrentLCS * ReData / 1000.0;    
}

float AXP173_Class::getVBUSVoltage() const {
    const float ADCLSB = 1.7 / 1000.0;
    uint8_t data[2] = {0, 0};
    if (!readRegister(0x5A, data, 2)) return NAN;
    uint16_t ReData = (data[0] << 4) | (data[1] & 0x0F);
    return ReData * ADCLSB;
}

float AXP173_Class::getVBUSCurrent() const {
    const float ADCLSB = 0.375;
    uint8_t data[2] = {0, 0};
    if (!readRegister(0x5C, data, 2)) return NAN;
    uint16_t ReData = (data[0] << 4) | (data[1] & 0x0F);
    return ReData * ADCLSB;
}

float AXP173_Class::getAXP173Temp() const {
    const float ADCLSB = 0.1;
    const float OFFSET_DEG_C = -144.7;
    uint8_t data[2] = {0, 0};
    if (!readRegister(0x5E, data, 2)) return NAN;
    uint16_t ReData = (data[0] << 4) | (data[1] & 0x0F);
    return OFFSET_DEG_C + ReData * ADCLSB;
}

/**
 * @brief Configure TS pin current source
 * @param chargingUA Current when charging: 20, 40, 60, or 80 (μA)
 * @param notChargingUA Current when not charging: 20, 40, 60, or 80 (μA)
 */
void AXP173_Class::setTSCurrent(uint8_t chargingUA, uint8_t notChargingUA) {
    // Convert μA to register value (20=0, 40=1, 60=2, 80=3)
    uint8_t chgSel = (chargingUA / 20) - 1;
    uint8_t noChgSel = (notChargingUA / 20) - 1;
    if (chgSel > 3) chgSel = 3;
    if (noChgSel > 3) noChgSel = 3;
    
    // Read current REG84H, preserve other bits
    _rmw(0x84, 0x0F, (uint8_t)((chgSel << 2) | noChgSel));
}

float AXP173_Class::getTSTemp() const {
    // Read TS pin ADC value (0x62-0x63)
    // AXP173_Class TS ADC: 12-bit, 0.8mV per LSB
    const float ADC_LSB_V = 0.0008f;  // 0.8mV = 0.0008V per LSB
    
    uint8_t data[2] = {0, 0};
    if (!readRegister(0x62, data, 2)) return NAN;
    uint16_t adcValue = (data[0] << 4) | (data[1] & 0x0F);
    
    // Calculate TS pin voltage in volts
    float tsVoltage = adcValue * ADC_LSB_V;
    
    // AXP173_Class TS pin uses CONSTANT CURRENT source
    // REG84H bits[3:2]: current when charging
    // REG84H bits[1:0]: current when NOT charging
    // Values: 00=20μA, 01=40μA, 10=60μA, 11=80μA
    // Default REG84H=0xFC: charging=80μA, not charging=20μA
    
    // Read REG84H to get actual current setting
    uint8_t reg84 = readRegister8(0x84);
    
    // Determine which current is being used based on charging state
    float I_TS;
    if (isCharging()) {
        // Use charging current (bits[3:2])
        uint8_t currentSel = (reg84 >> 2) & 0x03;
        I_TS = (currentSel + 1) * 20e-6f;  // 20/40/60/80 μA
    } else {
        // Use non-charging current (bits[1:0])
        uint8_t currentSel = reg84 & 0x03;
        I_TS = (currentSel + 1) * 20e-6f;  // 20/40/60/80 μA
    }
    
    // Calculate NTC resistance: V_TS = I_TS × R_NTC
    float rNTC = tsVoltage / I_TS;
    
    // Check for invalid readings (10K NTC: ~1K@85°C to ~30K@-10°C)
    if (rNTC < 500.0f || rNTC > 50000.0f) {
        return -273.15f;  // Invalid reading
    }
    
    // NTC thermistor parameters for 10K 1% NTC (typical B3380 or B3435)
    // Common 10K NTC B values: 3380, 3435, 3950
    // For battery temperature sensing, B3435 is typical
    const float R_NTC_25C = 10000.0f;  // NTC resistance at 25°C = 10K
    const float B_VALUE = 3435.0f;     // B parameter (adjust if needed)
    const float T_REF = 298.15f;       // Reference temperature 25°C in Kelvin
    
    // Steinhart-Hart B parameter equation:
    // 1/T = 1/T0 + (1/B) * ln(R/R0)
    float tempKelvin = 1.0f / (1.0f / T_REF + (1.0f / B_VALUE) * logf(rNTC / R_NTC_25C));
    float tempCelsius = tempKelvin - 273.15f;
    
    return tempCelsius;
}

/* ═══════════════════════════════════════════════════════════════
 *  Chip temperature ADC — REG 0x83 bit[7]
 * ═══════════════════════════════════════════════════════════════ */
void AXP173_Class::setChipTempEnable(bool state) {
    _rmw(0x83, state ? 0 : 0x80, state ? 0x80 : 0);
}

/* ═══════════════════════════════════════════════════════════════
 *  Power-on / Power-off / Long-press timing — REG 0x36
 *
 *  REG 0x36 layout:
 *    [7:6] POWERON_TIME   (boot key hold time)
 *    [5:4] LONG_PRESS_TIME
 *    [3]   Auto power-off when long-press > powerOff time
 *    [2]   PWROK signal delay (handled by setPWROKDelay)
 *    [1:0] POWEROFF_TIME  (shutdown hold time)
 * ═══════════════════════════════════════════════════════════════ */
void AXP173_Class::setPowerOnTime(POWERON_TIME onTime) {
    _rmw(0x36, 0xC0, (uint8_t)onTime & 0xC0);   /* bits [7:6] */
}

void AXP173_Class::setPowerOffTime(POWEROFF_TIME offTime) {
    _rmw(0x36, 0x03, (uint8_t)offTime & 0x03);  /* bits [1:0] */
}

void AXP173_Class::setLongPressTime(LONG_PRESS_TIME pressTime) {
    _rmw(0x36, 0x30, (uint8_t)(((uint8_t)pressTime << 4) & 0x30));  /* bits [5:4] */
}

void AXP173_Class::aoToPowerOFFEnabale() {
    _rmw(0x36, 0, 0x08);          /* bit [3] = 1 → auto power-off enable */
}

/* ═══════════════════════════════════════════════════════════════
 *  IRQ management — REG 0x40-0x43, 0x4A (enable) / 0x44-0x47, 0x4D (status)
 * ═══════════════════════════════════════════════════════════════ */

/** Clear all IRQ enable bits (safe initial state) */
void AXP173_Class::initIRQState() {
    writeRegister8(0x40, 0x00);
    writeRegister8(0x41, 0x00);
    writeRegister8(0x42, 0x00);
    writeRegister8(0x43, 0x00);
    writeRegister8(0x4A, 0x00);
    /* Clear any pending IRQ status by writing 1 to status bits */
    writeRegister8(0x44, 0xFF);
    writeRegister8(0x45, 0xFF);
    writeRegister8(0x46, 0xFF);
    writeRegister8(0x47, 0xFF);
    writeRegister8(0x4D, 0xFF);
}

/* ── Short press PEK key ────────────────────────────── */

void AXP173_Class::setShortPressEnabale() {
    /* REG 0x31 bit[3] = 1 → PEK short press trigger enable */
    _rmw(0x31, 0, 0x08);
}

bool AXP173_Class::getShortPressIRQState() {
    /* REG 0x44 bit[1] → PEK short press IRQ status */
    return (readRegister8(0x44) & 0x02) ? true : false;
}

void AXP173_Class::setShortPressIRQDisabale() {
    /* Write 1 to REG 0x44 bit[1] to clear the IRQ */
    writeRegister8(0x44, 0x02);
}

/* ── Long press PEK key ─────────────────────────────── */

bool AXP173_Class::getLongPressIRQState() {
    /* REG 0x44 bit[0] → PEK long press IRQ status */
    return (readRegister8(0x44) & 0x01) ? true : false;
}

void AXP173_Class::setLongPressIRQDisabale() {
    /* Write 1 to REG 0x44 bit[0] to clear the IRQ */
    writeRegister8(0x44, 0x01);
}

/* ═══════════════════════════════════════════════════════════════
 *  Sleep modes
 * ═══════════════════════════════════════════════════════════════ */

void AXP173_Class::prepareToSleep() {
    /* Disable all outputs except DCDC1 (keep MCU core alive for wake) */
    /* Application should call this before ESP32 light/deep sleep */
}

void AXP173_Class::lightSleep(uint64_t time_in_us) {
    prepareToSleep();
    esp_sleep_enable_timer_wakeup(time_in_us);
    esp_light_sleep_start();
}

void AXP173_Class::deepSleep(uint64_t time_in_us) {
    prepareToSleep();
    esp_sleep_enable_timer_wakeup(time_in_us);
    esp_deep_sleep_start();
}

void AXP173_Class::RestoreFromLightSleep() {
    /* Re-enable outputs after light sleep wake */
    setOutputEnable(OP_LDO2, true);
    setOutputEnable(OP_LDO3, true);
    setOutputEnable(OP_LDO4, true);
}