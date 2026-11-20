#include <gtest/gtest.h>
#include <array>
#include <functional>

#include "esphome/components/mirage/mirage.h"
#include "esphome/components/remote_base/mirage_protocol.h"

namespace esphome::mirage::testing {

// Records the last transmission instead of sending it
class CapturingTransmitter : public remote_base::RemoteTransmitterBase {
 public:
  CapturingTransmitter() : RemoteTransmitterBase(nullptr) {}
  int sends{0};
  remote_base::RawTimings last;

 protected:
  void send_internal(uint32_t, uint32_t) override {
    this->sends++;
    this->last = this->temp_.get_data();
  }
};

class TestMirage : public MirageClimate {
 public:
  using MirageClimate::control;
  using MirageClimate::on_receive;
  using MirageClimate::traits;
  using MirageClimate::transmit_state;
};

constexpr int32_t HEADER_MARK = 8360;
constexpr int32_t HEADER_SPACE = 4248;
constexpr int32_t BIT_MARK = 554;
constexpr int32_t ONE_SPACE = 1592;
constexpr int32_t ZERO_SPACE = 545;

uint8_t nibble_checksum(const MirageState &state) {
  uint8_t checksum = 0;
  for (uint8_t byte : state)
    checksum += static_cast<uint8_t>((byte >> 4) + (byte & 0x0F));
  return checksum;
}

remote_base::RawTimings encode_timings(const MirageState &state, optional<uint8_t> checksum_override = {}) {
  remote_base::RawTimings timings{HEADER_MARK, -HEADER_SPACE, BIT_MARK};
  auto push_byte = [&timings](uint8_t byte) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      timings.push_back((byte & (1U << bit)) ? -ONE_SPACE : -ZERO_SPACE);
      timings.push_back(BIT_MARK);
    }
  };
  for (uint8_t byte : state)
    push_byte(byte);
  push_byte(checksum_override.value_or(nibble_checksum(state)));
  return timings;
}

MirageState payload_of(const remote_base::RawTimings &timings) {
  MirageState out{};
  // 3 header/start entries, then 16 timing entries per byte (space, mark) for 14 data bytes + 1 checksum byte
  for (size_t byte_idx = 0; byte_idx < MIRAGE_STATE_LENGTH; byte_idx++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      const size_t space_idx = 3 + (byte_idx * 8 + bit) * 2;
      if (-timings[space_idx] > 1000)
        out[byte_idx] |= static_cast<uint8_t>(1U << bit);
    }
  }
  return out;
}

MirageState make_frame(uint8_t temp_c, uint8_t byte4, uint8_t byte5, uint8_t byte6 = 0x00, uint8_t byte8 = 0x00) {
  MirageState frame{};
  frame[0] = 0x56;
  frame[1] = static_cast<uint8_t>(0x5C + temp_c);
  frame[4] = byte4;
  frame[5] = byte5;
  frame[6] = byte6;
  frame[8] = byte8;
  return frame;
}

struct Fixture {
  TestMirage sut;
  CapturingTransmitter transmitter;

  Fixture() {
    this->sut.set_transmitter(&this->transmitter);
    this->sut.mode = climate::CLIMATE_MODE_COOL;
    this->sut.target_temperature = 24;
    this->sut.fan_mode = climate::CLIMATE_FAN_AUTO;
    this->sut.swing_mode = climate::CLIMATE_SWING_OFF;
    this->sut.preset = climate::CLIMATE_PRESET_NONE;
  }

  MirageState sent() const { return payload_of(this->transmitter.last); }

  bool receive(const remote_base::RawTimings &timings) {
    return this->sut.on_receive(remote_base::RemoteReceiveData(timings, 25, remote_base::TOLERANCE_MODE_PERCENTAGE));
  }
};

TEST(MirageTests, TraitsReportCoolOnlyModesPresetsAndRange) {
  Fixture f;
  auto traits = f.sut.traits();
  EXPECT_TRUE(traits.supports_mode(climate::CLIMATE_MODE_OFF));
  EXPECT_TRUE(traits.supports_mode(climate::CLIMATE_MODE_COOL));
  EXPECT_TRUE(traits.supports_mode(climate::CLIMATE_MODE_DRY));
  EXPECT_TRUE(traits.supports_mode(climate::CLIMATE_MODE_FAN_ONLY));
  EXPECT_FALSE(traits.supports_mode(climate::CLIMATE_MODE_HEAT));
  EXPECT_FALSE(traits.supports_mode(climate::CLIMATE_MODE_HEAT_COOL));
  EXPECT_FLOAT_EQ(traits.get_visual_min_temperature(), 16.0f);
  EXPECT_FLOAT_EQ(traits.get_visual_max_temperature(), 32.0f);
  EXPECT_TRUE(traits.supports_fan_mode(climate::CLIMATE_FAN_AUTO));
  EXPECT_TRUE(traits.supports_fan_mode(climate::CLIMATE_FAN_LOW));
  EXPECT_TRUE(traits.supports_fan_mode(climate::CLIMATE_FAN_MEDIUM));
  EXPECT_TRUE(traits.supports_fan_mode(climate::CLIMATE_FAN_HIGH));
  EXPECT_TRUE(traits.supports_swing_mode(climate::CLIMATE_SWING_OFF));
  EXPECT_TRUE(traits.supports_swing_mode(climate::CLIMATE_SWING_HORIZONTAL));
  EXPECT_TRUE(traits.supports_swing_mode(climate::CLIMATE_SWING_VERTICAL));
  EXPECT_TRUE(traits.supports_swing_mode(climate::CLIMATE_SWING_BOTH));
  EXPECT_TRUE(traits.supports_preset(climate::CLIMATE_PRESET_NONE));
  EXPECT_TRUE(traits.supports_preset(climate::CLIMATE_PRESET_BOOST));
  EXPECT_TRUE(traits.supports_preset(climate::CLIMATE_PRESET_SLEEP));
}

TEST(MirageTests, EncodesModesFansAndSwings) {
  struct Case {
    const char *name;
    climate::ClimateMode mode;
    climate::ClimateFanMode fan;
    climate::ClimateSwingMode swing;
    MirageState expected;
  };
  const Case cases[] = {
      {"cool auto off", climate::CLIMATE_MODE_COOL, climate::CLIMATE_FAN_AUTO, climate::CLIMATE_SWING_OFF,
       make_frame(24, 0x20, 0x00)},
      {"cool high horizontal", climate::CLIMATE_MODE_COOL, climate::CLIMATE_FAN_HIGH,
       climate::CLIMATE_SWING_HORIZONTAL, make_frame(24, 0x21, 0x01)},
      {"dry low vertical", climate::CLIMATE_MODE_DRY, climate::CLIMATE_FAN_LOW, climate::CLIMATE_SWING_VERTICAL,
       make_frame(24, 0x32, 0x02)},
      {"fan medium both", climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_SWING_BOTH,
       make_frame(24, 0x53, 0x03)},
      {"off preserves fan", climate::CLIMATE_MODE_OFF, climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_SWING_BOTH,
       make_frame(24, 0x03, 0xC0)},
  };
  for (const auto &c : cases) {
    SCOPED_TRACE(c.name);
    Fixture f;
    f.sut.mode = c.mode;
    f.sut.fan_mode = c.fan;
    f.sut.swing_mode = c.swing;
    f.sut.transmit_state();
    EXPECT_EQ(f.transmitter.sends, 1);
    EXPECT_EQ(f.sent(), c.expected);
  }
}

TEST(MirageTests, TargetIsRoundedClampedAndStored) {
  struct Case {
    float target;
    uint8_t sent;
  };
  for (const Case c : {Case{22.6f, 23}, Case{22.4f, 22}, Case{40.0f, 32}, Case{10.0f, 16}}) {
    SCOPED_TRACE(c.target);
    Fixture f;
    f.sut.target_temperature = c.target;
    f.sut.transmit_state();
    EXPECT_EQ(f.sent(), make_frame(c.sent, 0x20, 0x00));
    EXPECT_FLOAT_EQ(f.sut.target_temperature, c.sent);
  }
}

TEST(MirageTests, DisplayToggleIsOneShotAndIgnoredWhenOff) {
  Fixture f;
  f.sut.swing_mode = climate::CLIMATE_SWING_VERTICAL;
  f.sut.toggle_display();
  EXPECT_EQ(f.transmitter.sends, 1);
  EXPECT_EQ(f.sent(), make_frame(24, 0x20, 0x02 | 0x04));

  // Subsequent normal transmit clears the display toggle bit
  f.sut.transmit_state();
  EXPECT_EQ(f.transmitter.sends, 2);
  EXPECT_EQ(f.sent(), make_frame(24, 0x20, 0x02));

  // Pressing Display while OFF does not transmit or power the unit on
  f.sut.mode = climate::CLIMATE_MODE_OFF;
  f.sut.toggle_display();
  EXPECT_EQ(f.transmitter.sends, 2);
}

TEST(MirageTests, BoostAndSleepPresetsFollowModeRules) {
  Fixture f;
  f.sut.control(f.sut.make_call().set_preset(climate::CLIMATE_PRESET_BOOST));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_BOOST);
  EXPECT_EQ(f.sent(), make_frame(24, 0x20, 0x00, 0x00, 0x80));

  // Changing temperature keeps Boost active
  f.sut.control(f.sut.make_call().set_target_temperature(20));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_BOOST);
  EXPECT_EQ(f.sent(), make_frame(20, 0x20, 0x00, 0x00, 0x80));

  // Display toggle preserves Boost while adding the display bit
  f.sut.toggle_display();
  EXPECT_EQ(f.sent(), make_frame(20, 0x20, 0x04, 0x00, 0x80));

  // Switching to DRY clears Boost
  f.sut.control(f.sut.make_call().set_mode(climate::CLIMATE_MODE_DRY));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_NONE);
  EXPECT_EQ(f.sent(), make_frame(20, 0x30, 0x00, 0x00, 0x00));

  // Selecting Boost alone outside COOL does not transmit and stays None
  const int sends_before = f.transmitter.sends;
  f.sut.control(f.sut.make_call().set_preset(climate::CLIMATE_PRESET_BOOST));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_NONE);
  EXPECT_EQ(f.transmitter.sends, sends_before);

  // Sleep works in DRY mode
  f.sut.control(f.sut.make_call().set_preset(climate::CLIMATE_PRESET_SLEEP));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_SLEEP);
  EXPECT_EQ(f.sent(), make_frame(20, 0x30, 0x00, 0x08, 0x00));

  // Turning OFF clears Sleep
  f.sut.control(f.sut.make_call().set_mode(climate::CLIMATE_MODE_OFF));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_NONE);
  EXPECT_EQ(f.sent(), make_frame(20, 0x00, 0xC0, 0x00, 0x00));

  // Selecting Sleep alone while OFF does not transmit
  const int sends_when_off = f.transmitter.sends;
  f.sut.control(f.sut.make_call().set_preset(climate::CLIMATE_PRESET_SLEEP));
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_NONE);
  EXPECT_EQ(f.transmitter.sends, sends_when_off);
}

TEST(MirageTests, ReceivesStateFramesIncludingBoostAndSleep) {
  Fixture f;
  // COOL, 21C, LOW fan, BOTH swing + display toggle, BOOST active
  EXPECT_TRUE(f.receive(encode_timings(make_frame(21, 0x22, 0x03 | 0x04, 0x00, 0x80))));
  EXPECT_EQ(f.sut.mode, climate::CLIMATE_MODE_COOL);
  EXPECT_FLOAT_EQ(f.sut.target_temperature, 21.0f);
  EXPECT_EQ(f.sut.fan_mode, climate::CLIMATE_FAN_LOW);
  EXPECT_EQ(f.sut.swing_mode, climate::CLIMATE_SWING_BOTH);
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_BOOST);

  // SLEEP active
  EXPECT_TRUE(f.receive(encode_timings(make_frame(22, 0x20, 0x02, 0x08, 0x00))));
  EXPECT_EQ(f.sut.mode, climate::CLIMATE_MODE_COOL);
  EXPECT_FLOAT_EQ(f.sut.target_temperature, 22.0f);
  EXPECT_EQ(f.sut.swing_mode, climate::CLIMATE_SWING_VERTICAL);
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_SLEEP);

  // OFF preserves swing and decodes persisted fan speed while clearing preset
  EXPECT_TRUE(f.receive(encode_timings(make_frame(22, 0x03, 0xC0, 0x00, 0x00))));
  EXPECT_EQ(f.sut.mode, climate::CLIMATE_MODE_OFF);
  EXPECT_EQ(f.sut.fan_mode, climate::CLIMATE_FAN_MEDIUM);
  EXPECT_EQ(f.sut.swing_mode, climate::CLIMATE_SWING_VERTICAL);
  EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_NONE);
}

TEST(MirageTests, IgnoresOutOfRangeTemperatureButKeepsRest) {
  Fixture f;
  auto frame = make_frame(24, 0x51, 0x01);
  frame[1] = 0x00;  // Below 0x5C offset -> negative temperature
  EXPECT_TRUE(f.receive(encode_timings(frame)));
  EXPECT_EQ(f.sut.mode, climate::CLIMATE_MODE_FAN_ONLY);
  EXPECT_FLOAT_EQ(f.sut.target_temperature, 24.0f);
  EXPECT_EQ(f.sut.fan_mode, climate::CLIMATE_FAN_HIGH);
  EXPECT_EQ(f.sut.swing_mode, climate::CLIMATE_SWING_HORIZONTAL);
}

TEST(MirageTests, RejectsBadFramesWithoutChangingState) {
  struct Case {
    const char *name;
    std::function<remote_base::RawTimings()> make_bad;
  };
  const Case cases[] = {
      {"bad header byte",
       []() {
         auto frame = make_frame(20, 0x32, 0x02);
         frame[0] = 0x55;
         return encode_timings(frame);
       }},
      {"bad checksum",
       []() {
         auto frame = make_frame(20, 0x32, 0x02);
         return encode_timings(frame, static_cast<uint8_t>(nibble_checksum(frame) ^ 0xFF));
       }},
      {"truncated timings",
       []() {
         auto timings = encode_timings(make_frame(20, 0x32, 0x02));
         timings.resize(timings.size() - 20);
         return timings;
       }},
      {"unsupported mode", []() { return encode_timings(make_frame(20, 0x10, 0x00)); }},
  };
  for (const auto &c : cases) {
    SCOPED_TRACE(c.name);
    Fixture f;
    EXPECT_FALSE(f.receive(c.make_bad()));
    EXPECT_EQ(f.sut.mode, climate::CLIMATE_MODE_COOL);
    EXPECT_FLOAT_EQ(f.sut.target_temperature, 24.0f);
    EXPECT_EQ(f.sut.fan_mode, climate::CLIMATE_FAN_AUTO);
    EXPECT_EQ(f.sut.swing_mode, climate::CLIMATE_SWING_OFF);
    EXPECT_EQ(f.sut.preset, climate::CLIMATE_PRESET_NONE);
  }
}

TEST(MirageTests, OwnTransmissionIsAcceptedByReceiver) {
  Fixture tx;
  tx.sut.mode = climate::CLIMATE_MODE_COOL;
  tx.sut.target_temperature = 19;
  tx.sut.fan_mode = climate::CLIMATE_FAN_HIGH;
  tx.sut.swing_mode = climate::CLIMATE_SWING_VERTICAL;
  tx.sut.control(tx.sut.make_call().set_preset(climate::CLIMATE_PRESET_BOOST));

  Fixture rx;
  EXPECT_TRUE(rx.receive(tx.transmitter.last));
  EXPECT_EQ(rx.sut.mode, climate::CLIMATE_MODE_COOL);
  EXPECT_FLOAT_EQ(rx.sut.target_temperature, 19.0f);
  EXPECT_EQ(rx.sut.fan_mode, climate::CLIMATE_FAN_HIGH);
  EXPECT_EQ(rx.sut.swing_mode, climate::CLIMATE_SWING_VERTICAL);
  EXPECT_EQ(rx.sut.preset, climate::CLIMATE_PRESET_BOOST);
}

}  // namespace esphome::mirage::testing
