// Copyright (c) M5Stack. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#if defined (ESP_PLATFORM)

#include "BMI270_Class.hpp"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "BMI270_config.inl"
#include "BMI270_protocol.hpp"

  BMI270_Class::~BMI270_Class() {}
  BMI270_Class::BMI270_Class(std::uint8_t i2c_addr, std::uint32_t freq, I2C_Class* i2c)
  : IMU_Base ( i2c_addr, freq, i2c )
  {}

  bool BMI270_Class::_upload_file(const uint8_t *config_data, size_t index, size_t write_len)
  {
    return bmi270_protocol::upload(
      config_data, index, write_len,
      [this](const std::uint8_t address[2]) {
        return writeRegister(INIT_ADDR_0, address, 2);
      },
      [this](const std::uint8_t* data, std::size_t length) {
        return writeRegister(INIT_DATA_ADDR, data, length);
      });
  }

  IMU_Base::imu_spec_t BMI270_Class::begin(I2C_Class* i2c)
  {
    _init = false;
    if (i2c)
    {
      _i2c = i2c;
    }

    // Wait for sensor power stabilization
    vTaskDelay(10);
    
    // Perform soft reset first to ensure clean state
    Serial.println("  Performing BMI270 soft reset...");
    if (!writeRegister8(CMD_REG_ADDR, SOFT_RESET_CMD)) return imu_spec_none;
    vTaskDelay(2);  // BMI270 datasheet: startup time after reset is 2ms
    
    // Wait for chip to be ready
    int retry = 16;
    while (0 == readRegister8(PWR_CONF_ADDR) && --retry) {
      vTaskDelay(1);
    }
    if (retry == 0) {
      Serial.println("  [ERROR] BMI270 not responding after reset");
      return imu_spec_none;
    }
    
    // Read first 16 registers for debugging
    Serial.println("  Reading BMI270 register dump (0x00-0x0F):");
    Serial.print("  ");
    for (int reg = 0; reg < 16; reg++) {
      uint8_t val = readRegister8(reg);
      Serial.printf("%02X ", val);
      if ((reg + 1) % 8 == 0) Serial.print("\n  ");
    }
    Serial.println();
    
    // Now read chip ID
    uint8_t chip_id = 0;
    if (!readRegister(CHIP_ID_ADDR, &chip_id, 1)) return imu_spec_none;
    Serial.printf("  BMI270 Chip ID: 0x%02X (expected 0x24 for BMI270)\n", chip_id);
    
    if (chip_id != 0x24) 
    {
      Serial.println("  [ERROR] Chip ID mismatch! Possible causes:");
      Serial.println("    - Wrong sensor installed (check hardware)");
      Serial.println("    - I2C communication issue");
      Serial.println("    - Sensor not powered properly");
      Serial.println("  Known Chip IDs: 0x19=MPU6886, 0xD1=BMI160, 0x24=BMI270");
      return imu_spec_none;
    }

    // Continue with initialization
    if (!writeRegister8(PWR_CONF_ADDR, 0x00)) return imu_spec_none; // Power save disabled.
    vTaskDelay(1);
    if (!writeRegister8(INIT_CTRL_ADDR, 0x00)
        || !_upload_file(bmi270_config_file, 0, sizeof(bmi270_config_file))
        || !writeRegister8(INIT_CTRL_ADDR, 0x01)) return imu_spec_none;

    imu_spec_t spec = (imu_spec_t)(imu_spec_accel | imu_spec_gyro);
    retry = 25;
    uint8_t status = 0;
    do {
      vTaskDelay(1);
      if (!readRegister(INTERNAL_STATUS_ADDR, &status, 1)) return imu_spec_none;
      if (bmi270_protocol::initReady(status)) break;
    } while (--retry);
    if (!bmi270_protocol::initReady(status)) return imu_spec_none;

// 以下は AUXセンサとしてBMM150が接続されている想定の設定
    bool mag_ready = auxSetupMode(0x10); // 0x10 = BMM150 I2C Addr
    mag_ready = mag_ready && auxWriteRegister8(0x4B, 0x83); // software reset + power on
    std::uint8_t who_am_i = 0;
    mag_ready = mag_ready && auxReadRegister8(0x40, who_am_i); // 0x40 = WhoAmI
    if (mag_ready && who_am_i == 0x32)
    {
      mag_ready = auxWriteRegister8(0x4C, 0x38)
        && writeRegister8(AUX_IF_CONF_ADDR, 0x4F)
        && writeRegister8(AUX_RD_ADDR, 0x42);
    }
    else mag_ready = false;

    if (!writeRegister8(PWR_CTRL_ADDR, mag_ready ? 0x0F : 0x0E)) return imu_spec_none;
    uint8_t power = 0;
    if (!readRegister(PWR_CTRL_ADDR, &power, 1) || (power & 0x06) != 0x06)
      return imu_spec_none;
    if (!writeRegister8(INT_MAP_DATA_ADDR, 0xFF)) return imu_spec_none;
    _init = true;
    if (mag_ready && (power & 0x01)) spec = (imu_spec_t)(spec | imu_spec_mag);
    return spec;
  }

  bool BMI270_Class::auxSetupMode(std::uint8_t i2c_addr)
  {
    return writeRegister8(IF_CONF_ADDR, 0x20)  // AUX I2C enable.
        && writeRegister8(PWR_CONF_ADDR, 0x00) // Power save disabled.
        && writeRegister8(PWR_CTRL_ADDR, 0x0E) // AUX sensor disable.
        && writeRegister8(AUX_IF_CONF_ADDR, 0x80)
        && writeRegister8(AUX_DEV_ID_ADDR , i2c_addr << 1);
  }

  bool BMI270_Class::auxWriteRegister8(std::uint8_t reg, std::uint8_t data)
  {
    if (!writeRegister8(AUX_WR_DATA_ADDR, data) // AUXセンサに書き込む値
        || !writeRegister8(AUX_WR_ADDR, reg)) return false;
    int retry = 3;
    while (retry--) {
      std::uint8_t status = 0;
      if (!readRegister(STATUS_ADDR, &status, 1)) return false;
      if (!(status & 0b100)) return true;
      vTaskDelay(1);
    }
    return false;
  }

  bool BMI270_Class::auxReadRegister8(std::uint8_t reg, std::uint8_t& result)
  {
    if (!writeRegister8(AUX_IF_CONF_ADDR, 0x80) // enable read write. Burst length 1
        || !writeRegister8(AUX_RD_ADDR, reg)) return false; // AUXセンサから読み取るレジスタ番号
    int retry = 3;
    while (retry--) {
      std::uint8_t status = 0;
      if (!readRegister(STATUS_ADDR, &status, 1)) return false;
      if (!(status & 0b100))
        return readRegister(AUX_X_LSB_ADDR, &result, 1);
      vTaskDelay(1);
    }
    return false;
  }

  std::uint8_t BMI270_Class::auxReadRegister8(std::uint8_t reg)
  {
    std::uint8_t result = 0;
    auxReadRegister8(reg, result);
    return result;
  }

  std::uint8_t BMI270_Class::WhoAmI(void)
  {
    return readRegister8(CHIP_ID_ADDR);
  }

  IMU_Base::imu_spec_t BMI270_Class::getImuRawData(imu_raw_data_t* data) const
  {
    imu_spec_t res = imu_spec_none;
    std::uint8_t intstat = readRegister8(INT_STATUS_1_ADDR);
    if (intstat & 0xE0)
    {
      std::int16_t buf[10];
      if (readRegister(AUX_X_LSB_ADDR, (std::uint8_t*)buf, 20))
      {
        if (intstat & 0x80u)
        {
          data->accel.x = buf[4];
          data->accel.y = buf[5];
          data->accel.z = buf[6];
          res = (imu_spec_t)(res | imu_spec_accel);
        }
        if (intstat & 0x40u)
        {
          data->gyro.x = buf[7];
          data->gyro.y = buf[8];
          data->gyro.z = buf[9];
          res = (imu_spec_t)(res | imu_spec_gyro);
        }
        if (intstat & 0x20u)
        {
          data->mag.x = buf[0] >> 2;
          data->mag.y = buf[1] >> 2;
          data->mag.z = buf[2] & 0xFFFE;
          res = (imu_spec_t)(res | imu_spec_mag);
        }
      }
    }
    return res;
  }

  void BMI270_Class::getConvertParam(imu_convert_param_t* param) const
  {
    uint8_t accel = 0, gyro = 0;
    if (readRegister(0x41, &accel, 1))
      param->accel_res = static_cast<float>(2u << (accel & 3u)) / 32768.f;
    if (readRegister(0x43, &gyro, 1) && (gyro & 7u) <= 4)
      param->gyro_res = (2000.f / (1u << (gyro & 7u))) / 32768.f;
    param->mag_res = 10.0f * 4912.0f / 32760.0f;
    param->temp_offset = 23.0f;
    param->temp_res = 1.0f / 512.0f;
  }

  bool BMI270_Class::getTempAdc(int16_t *t) const
  {
    std::int16_t buf;
    bool res = readRegister(TEMPERATURE_0_ADDR, (std::uint8_t*)&buf, 2);
    if (res) { *t = buf; }
    return res;
  }

  uint8_t BMI270_Class::getGyroRange() const
  {
    uint8_t value = 0;
    if (!readRegister(0x43, &value, 1) || (value & 7u) > 4) return 255;
    return 4 - (value & 7u);
  }

  bool BMI270_Class::setGyroRange(uint8_t range)
  {
    uint8_t value = 0;
    if (range > 4 || !readRegister(0x43, &value, 1)) return false;
    value = (value & ~7u) | (4 - range);
    if (!writeRegister8(0x43, value)) return false;
    delayMicroseconds(450);
    return getGyroRange() == range;
  }

#endif
