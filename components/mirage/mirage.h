#pragma once

#include <array>
#include <cstdint>

#include "esphome/components/climate_ir/climate_ir.h"

namespace esphome::mirage {

// Temperature range in degrees Celsius
static constexpr uint8_t MIRAGE_TEMP_MIN = 16;
static constexpr uint8_t MIRAGE_TEMP_MAX = 32;

// 14 data bytes (the 15th byte is the nibble-sum checksum handled by remote_base::MirageProtocol)
static constexpr size_t MIRAGE_STATE_LENGTH = 14;

enum Model : uint8_t {
  MODEL_KKG29A_C1 = 0,
};

using MirageState = std::array<uint8_t, MIRAGE_STATE_LENGTH>;

struct MirageClimateData {
  climate::ClimateMode mode{climate::CLIMATE_MODE_OFF};
  optional<uint8_t> target_temperature;
  climate::ClimateFanMode fan_mode{climate::CLIMATE_FAN_AUTO};
  optional<climate::ClimateSwingMode> swing_mode;
  climate::ClimatePreset preset{climate::CLIMATE_PRESET_NONE};
  bool display_toggle{false};
};

class MirageClimateCodec {
 public:
  static MirageState encode(Model model, const MirageClimateData &data);
  /// Decode a 14-byte payload whose timing and checksum remote_base::MirageProtocol already verified.
  static optional<MirageClimateData> decode(Model model, const MirageState &state);
};

class MirageClimate final : public climate_ir::ClimateIR {
 public:
  MirageClimate()
      : climate_ir::ClimateIR(
            MIRAGE_TEMP_MIN, MIRAGE_TEMP_MAX, 1.0f, true, true,
            {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
             climate::CLIMATE_FAN_HIGH},
            {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL,
             climate::CLIMATE_SWING_BOTH},
            {climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP}) {
    this->set_supports_heat(false);
    this->set_supports_heat_cool(false);
  }

  void set_model(Model model) { this->model_ = model; }
  void toggle_display();

 protected:
  void control(const climate::ClimateCall &call) override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  void transmit_state_(bool toggle_display);

  Model model_{MODEL_KKG29A_C1};
  uint32_t last_transmit_time_{0};
  bool has_transmitted_{false};
};

}  // namespace esphome::mirage
