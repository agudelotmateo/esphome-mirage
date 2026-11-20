#include "mirage.h"

#include <algorithm>
#include <cmath>

#include "esphome/components/remote_base/mirage_protocol.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::mirage {

ESPHOME_LOG_TAG(TAG, "mirage.climate");

// Mirage KKG29A-C1 14-byte payload (byte 14 checksum is handled by remote_base::MirageProtocol):
//   Byte 0:   0x56 fixed header
//   Byte 1:   target temperature in °C + 0x5C (16-32 °C -> 0x6C-0x7C)
//   Byte 4:   bits 0-1 fan speed (0=auto, 1=high, 2=low, 3=medium)
//             bits 4-6 mode (0x20=cool, 0x30=dry, 0x50=fan_only; 0x00 when off)
//   Byte 5:   bit 0 horizontal swing (0x01)
//             bit 1 vertical swing (0x02)
//             bit 2 display LED toggle (0x04, one-shot toggle; any subsequent frame wakes the LED)
//             bits 6-7 power (0x00=on, 0xC0=off)
//   Byte 6:   bit 3 sleep mode (0x08, stateful)
//   Byte 8:   bit 7 turbo/boost mode (0x80, stateful, cool mode only; disables swing on the unit)
//   Bytes 2-3, 7, 9-13: unused (0x00)
static constexpr uint8_t MIRAGE_HEADER = 0x56;
static constexpr uint8_t MIRAGE_TEMP_OFFSET = 0x5C;
static constexpr uint8_t MIRAGE_TEMP_DEFAULT = 24;

static constexpr uint8_t MIRAGE_MODE_MASK = 0x70;
static constexpr uint8_t MIRAGE_MODE_COOL = 0x20;
static constexpr uint8_t MIRAGE_MODE_DRY = 0x30;
static constexpr uint8_t MIRAGE_MODE_FAN = 0x50;

static constexpr uint8_t MIRAGE_FAN_MASK = 0x03;
static constexpr uint8_t MIRAGE_FAN_AUTO = 0x00;
static constexpr uint8_t MIRAGE_FAN_HIGH = 0x01;
static constexpr uint8_t MIRAGE_FAN_LOW = 0x02;
static constexpr uint8_t MIRAGE_FAN_MED = 0x03;

static constexpr uint8_t MIRAGE_SWING_MASK = 0x03;
static constexpr uint8_t MIRAGE_SWING_OFF = 0x00;
static constexpr uint8_t MIRAGE_SWING_HORIZONTAL = 0x01;
static constexpr uint8_t MIRAGE_SWING_VERTICAL = 0x02;
static constexpr uint8_t MIRAGE_SWING_BOTH = 0x03;

static constexpr uint8_t MIRAGE_DISPLAY_TOGGLE_MASK = 0x04;
static constexpr uint8_t MIRAGE_POWER_OFF = 0xC0;
static constexpr uint8_t MIRAGE_SLEEP_MASK = 0x08;
static constexpr uint8_t MIRAGE_TURBO_MASK = 0x80;

// Ignore IR frames received right after our own transmission to avoid echo on combined TX/RX hardware
static constexpr uint32_t MIRAGE_TRANSMIT_GUARD_MS = 500;

// Mappings use if-else rather than switch to avoid RAM lookup tables on ESP8266.
static uint8_t encode_mode(climate::ClimateMode mode) {
  if (mode == climate::CLIMATE_MODE_COOL)
    return MIRAGE_MODE_COOL;
  if (mode == climate::CLIMATE_MODE_DRY)
    return MIRAGE_MODE_DRY;
  if (mode == climate::CLIMATE_MODE_FAN_ONLY)
    return MIRAGE_MODE_FAN;
  return 0x00;
}

static optional<climate::ClimateMode> decode_mode(uint8_t mode) {
  if (mode == MIRAGE_MODE_COOL)
    return climate::CLIMATE_MODE_COOL;
  if (mode == MIRAGE_MODE_DRY)
    return climate::CLIMATE_MODE_DRY;
  if (mode == MIRAGE_MODE_FAN)
    return climate::CLIMATE_MODE_FAN_ONLY;
  return {};
}

static uint8_t encode_fan_mode(climate::ClimateFanMode fan_mode) {
  if (fan_mode == climate::CLIMATE_FAN_HIGH)
    return MIRAGE_FAN_HIGH;
  if (fan_mode == climate::CLIMATE_FAN_LOW)
    return MIRAGE_FAN_LOW;
  if (fan_mode == climate::CLIMATE_FAN_MEDIUM)
    return MIRAGE_FAN_MED;
  return MIRAGE_FAN_AUTO;
}

static climate::ClimateFanMode decode_fan_mode(uint8_t fan) {
  if (fan == MIRAGE_FAN_HIGH)
    return climate::CLIMATE_FAN_HIGH;
  if (fan == MIRAGE_FAN_LOW)
    return climate::CLIMATE_FAN_LOW;
  if (fan == MIRAGE_FAN_MED)
    return climate::CLIMATE_FAN_MEDIUM;
  return climate::CLIMATE_FAN_AUTO;
}

static uint8_t encode_swing_mode(climate::ClimateSwingMode swing_mode) {
  if (swing_mode == climate::CLIMATE_SWING_BOTH)
    return MIRAGE_SWING_BOTH;
  if (swing_mode == climate::CLIMATE_SWING_VERTICAL)
    return MIRAGE_SWING_VERTICAL;
  if (swing_mode == climate::CLIMATE_SWING_HORIZONTAL)
    return MIRAGE_SWING_HORIZONTAL;
  return MIRAGE_SWING_OFF;
}

static climate::ClimateSwingMode decode_swing_mode(uint8_t swing) {
  if (swing == MIRAGE_SWING_BOTH)
    return climate::CLIMATE_SWING_BOTH;
  if (swing == MIRAGE_SWING_VERTICAL)
    return climate::CLIMATE_SWING_VERTICAL;
  if (swing == MIRAGE_SWING_HORIZONTAL)
    return climate::CLIMATE_SWING_HORIZONTAL;
  return climate::CLIMATE_SWING_OFF;
}

MirageState MirageClimateCodec::encode(Model /*model*/, const MirageClimateData &data) {
  MirageState state{};
  state[0] = MIRAGE_HEADER;

  const uint8_t temp = clamp<uint8_t>(data.target_temperature.value_or(MIRAGE_TEMP_DEFAULT), MIRAGE_TEMP_MIN,
                                      MIRAGE_TEMP_MAX);
  state[1] = static_cast<uint8_t>(MIRAGE_TEMP_OFFSET + temp);
  state[4] = encode_fan_mode(data.fan_mode);

  if (data.mode == climate::CLIMATE_MODE_OFF) {
    state[5] = MIRAGE_POWER_OFF;
    return state;
  }

  state[4] |= encode_mode(data.mode);
  state[5] = encode_swing_mode(data.swing_mode.value_or(climate::CLIMATE_SWING_OFF));
  if (data.display_toggle)
    state[5] |= MIRAGE_DISPLAY_TOGGLE_MASK;
  if (data.preset == climate::CLIMATE_PRESET_SLEEP)
    state[6] |= MIRAGE_SLEEP_MASK;
  if (data.mode == climate::CLIMATE_MODE_COOL && data.preset == climate::CLIMATE_PRESET_BOOST)
    state[8] |= MIRAGE_TURBO_MASK;

  return state;
}

optional<MirageClimateData> MirageClimateCodec::decode(Model /*model*/, const MirageState &state) {
  if (state[0] != MIRAGE_HEADER)
    return {};

  MirageClimateData data;
  data.fan_mode = decode_fan_mode(state[4] & MIRAGE_FAN_MASK);

  const int temp = static_cast<int>(state[1]) - MIRAGE_TEMP_OFFSET;
  if (temp >= MIRAGE_TEMP_MIN && temp <= MIRAGE_TEMP_MAX)
    data.target_temperature = static_cast<uint8_t>(temp);

  if ((state[5] & MIRAGE_POWER_OFF) == MIRAGE_POWER_OFF) {
    data.mode = climate::CLIMATE_MODE_OFF;
    return data;
  }

  auto mode = decode_mode(state[4] & MIRAGE_MODE_MASK);
  if (!mode.has_value())
    return {};
  data.mode = *mode;

  data.swing_mode = decode_swing_mode(state[5] & MIRAGE_SWING_MASK);
  data.display_toggle = (state[5] & MIRAGE_DISPLAY_TOGGLE_MASK) != 0;

  if (data.mode == climate::CLIMATE_MODE_COOL && (state[8] & MIRAGE_TURBO_MASK)) {
    data.preset = climate::CLIMATE_PRESET_BOOST;
  } else if (state[6] & MIRAGE_SLEEP_MASK) {
    data.preset = climate::CLIMATE_PRESET_SLEEP;
  }

  return data;
}

void MirageClimate::toggle_display() {
  if (this->mode == climate::CLIMATE_MODE_OFF)
    return;
  this->transmit_state_(true);
}

void MirageClimate::control(const climate::ClimateCall &call) {
  const climate::ClimateMode next_mode = call.get_mode().value_or(this->mode);
  const bool only_preset = !call.get_mode().has_value() && !call.get_target_temperature().has_value() &&
                           !call.get_fan_mode().has_value() && !call.get_swing_mode().has_value() &&
                           call.get_preset().has_value();
  if (only_preset &&
      (next_mode == climate::CLIMATE_MODE_OFF ||
       (*call.get_preset() == climate::CLIMATE_PRESET_BOOST && next_mode != climate::CLIMATE_MODE_COOL))) {
    this->preset = climate::CLIMATE_PRESET_NONE;
    this->publish_state();
    return;
  }
  climate_ir::ClimateIR::control(call);
}

void MirageClimate::transmit_state() { this->transmit_state_(false); }

void MirageClimate::transmit_state_(bool toggle_display) {
  if (this->mode == climate::CLIMATE_MODE_OFF ||
      (this->preset == climate::CLIMATE_PRESET_BOOST && this->mode != climate::CLIMATE_MODE_COOL)) {
    this->preset = climate::CLIMATE_PRESET_NONE;
  }

  const float raw_temp = std::isfinite(this->target_temperature) ? this->target_temperature : MIRAGE_TEMP_DEFAULT;
  const auto temp = static_cast<uint8_t>(
      clamp<float>(roundf(raw_temp), static_cast<float>(MIRAGE_TEMP_MIN), static_cast<float>(MIRAGE_TEMP_MAX)));
  this->target_temperature = temp;

  const MirageClimateData climate_data{
      .mode = this->mode,
      .target_temperature = temp,
      .fan_mode = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO),
      .swing_mode = this->swing_mode,
      .preset = this->preset.value_or(climate::CLIMATE_PRESET_NONE),
      .display_toggle = toggle_display,
  };
  const MirageState state = MirageClimateCodec::encode(this->model_, climate_data);

  ESP_LOGV_HEX(TAG, state.data(), state.size(), MIRAGE_STATE_LENGTH, "Sending: %s");

  this->last_transmit_time_ = millis();
  this->has_transmitted_ = true;

  auto transmit = this->transmitter_->transmit();
  remote_base::MirageProtocol().encode(transmit.get_data(), state.data(), state.size());
  transmit.perform();
}

bool MirageClimate::on_receive(remote_base::RemoteReceiveData data) {
  if (this->has_transmitted_ && millis() - this->last_transmit_time_ < MIRAGE_TRANSMIT_GUARD_MS) {
    ESP_LOGV(TAG, "Blocked receive because of recent transmission");
    return false;
  }

  remote_base::MirageProtocol protocol;
  auto decoded_raw = protocol.decode(data);
  if (!decoded_raw.has_value() || decoded_raw->data.size() < MIRAGE_STATE_LENGTH)
    return false;

  MirageState state;
  std::copy_n(decoded_raw->data.data(), MIRAGE_STATE_LENGTH, state.data());

  auto decoded = MirageClimateCodec::decode(this->model_, state);
  if (!decoded.has_value()) {
    ESP_LOGD_HEX(TAG, state.data(), state.size(), MIRAGE_STATE_LENGTH, "Mirage frame rejected: %s");
    return false;
  }

  ESP_LOGV_HEX(TAG, state.data(), state.size(), MIRAGE_STATE_LENGTH, "Received: %s");

  this->mode = decoded->mode;
  if (decoded->target_temperature.has_value())
    this->target_temperature = *decoded->target_temperature;
  this->fan_mode = decoded->fan_mode;
  if (decoded->swing_mode.has_value())
    this->swing_mode = *decoded->swing_mode;
  this->preset = decoded->preset;

  this->publish_state();
  return true;
}

}  // namespace esphome::mirage
