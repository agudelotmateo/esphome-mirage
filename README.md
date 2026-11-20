# ESPHome Mirage IR Climate Component

ESPHome external component implementing the **Mirage `KKG29A-C1`** 120-bit IR climate protocol on top of ESPHome's built-in `climate_ir` and `remote_base::MirageProtocol`.

Tested on cool-only whitelabeled AC units using the `KKG29A-C1` remote (such as Simply Turn On / Finlandek sold by Éxito Colombia, and compatible Tronitechnik / Mirage units).

## Requirements

- **ESPHome 2026.11.0 or newer** (uses `ApplyButton`, `Model.options()`, `remote_base.request_protocol()`, and `ESP_LOG*_HEX` introduced in 2026.11).

## Supported Features

- **Climate (`climate: - platform: mirage`)**
  - Modes: `OFF`, `COOL`, `DRY`, `FAN_ONLY` (cool-only; `supports_heat` and `supports_heat_cool` are disabled)
  - Temperature range: `16–32 °C` (1 °C step)
  - Fan speeds: `AUTO`, `LOW`, `MEDIUM`, `HIGH`
  - Swing modes: `OFF`, `HORIZONTAL`, `VERTICAL`, `BOTH`
  - Presets: `NONE`, `BOOST` (Turbo, `COOL` mode only; persists across temperature changes), `SLEEP`
  - Optional IR receiver synchronization (`receiver_id`)
- **Display toggle (`button: - platform: mirage`)**
  - `display`: one-shot toggle for the unit's LED display (ignored while the unit is off)

## Example Configuration

```yaml
external_components:
  - source: github://agudelotmateo/esphome-mirage@main
    components: [ mirage ]

climate:
  - platform: mirage
    name: Air Conditioner
    model: KKG29A-C1
    receiver_id: infrared_receiver_id
    sensor: temperature_sensor_id

button:
  - platform: mirage
    display:
      name: Air Conditioner Display
```

*(When only one `mirage` climate entity exists on the device, `mirage_id` is resolved automatically. For multiple Mirage units on one ESP, set `id:` on each `climate` entry and `mirage_id:` on each `button` entry.)*

## Protocol Layout (`KKG29A-C1`)

14 data bytes (plus byte 14 nibble-sum checksum handled by `remote_base::MirageProtocol`):

| Byte | Bits | Description |
| :--- | :--- | :--- |
| `0` | `0–7` | Fixed header (`0x56`) |
| `1` | `0–7` | Target temperature in °C + `0x5C` (`16–32 °C` → `0x6C–0x7C`) |
| `4` | `0–1` | Fan speed (`0` Auto, `1` High, `2` Low, `3` Medium) |
| `4` | `4–6` | Mode (`0x20` Cool, `0x30` Dry, `0x50` Fan Only; `0x00` when Off) |
| `5` | `0` | Horizontal swing (`0x01`) |
| `5` | `1` | Vertical swing (`0x02`) |
| `5` | `2` | Display LED toggle (`0x04`, one-shot) |
| `5` | `6–7` | Power (`0x00` On, `0xC0` Off) |
| `6` | `3` | Sleep mode (`0x08`, stateful) |
| `8` | `7` | Turbo / Boost mode (`0x80`, stateful, `COOL` mode only) |

## Repository Layout

Structured for direct use as an ESPHome external component (`components/mirage/`) while mirroring `esphome/esphome` conventions (`tests/components/mirage/` with GoogleTest unit tests and build test configs) to simplify upstreaming as a PR.
