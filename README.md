# Overview

An open source alternative for the Panasonic wi-fi adapter that works locally without the cloud.

# Documentation

* [Example configuration](panasonic.yaml) with all options
* [Changelog](CHANGELOG.md): what changed, and what to check in an existing configuration
* [Vane control and simulated swing](docs/swing.md): how Panasonic controls the vanes and why `auto` does not swing in heating mode
* Hardware installation for [CZ-TACG1 (CN-CNT)](README.CZTACG1.md) and [DNSK-P11 (CN-WLAN)](README.DNSKP11.md)

# Features

* Control your AC locally via Home Assistant, MQTT or directly
* Instantly control the AC without any delay like in the Comfort Cloud app
* Receive live reports and state from the AC
* Uses the UART interface on the AC instead of the IR interface
* Provides a drop-in replacement for the Panasonic DNSK-P11 and the CZ-TACG1 wifi module
* Works together with the IR remote: settings made with the remote are kept when Home Assistant sends a command (CZ-TACG1)
* Adaptive polling and simulated vane swing in every operating mode, also in heating (CZ-TACG1)

# Supported hardware

This library works with both the CN-CNT port and the CN-WLAN port. CN-WLAN is only available on newer units. Either port can be used on units that have both ports regardless of the usage of the other port (ie. it is possible to leave the DNSK-P11 connected to CN-WLAN and connect the ESP to CN-CNT).

Works on the ESP8266 but ESP32 is preferred for the multiple hardware serial ports.

# Requirements

* ESP32 (or ESP8266) ([supported by ESPHome](https://esphome.io/#devices))
* 5V to 3.3V bi-directional Logic Converter (minimum 2 channels, available as pre-soldered prototyping boards)
* Female-Female Jumper cables
* Soldering iron
* Wires to solder from Logic converter to ESP
* Heat shrink
* ESPHome 2026.4.0 or newer
* Home Assistant 2025.11.0 or newer

# Notes

* **Make sure to disconnect mains power before opening your AC, the mains contacts are exposed and can be touched by accident!**
* **Do not connect your ESP32/ESP8266 directly to the AC, the AC uses 5V while the ESPs use 3.3V!**
* **While installation is fairly straightforward I do not take any responsibility for any damage done to you or your AC during installation**
* The DNSK-P11 and the CZ-TACG1 use different types of connectors, make sure to connect to the correct one

# Software installation

This software installation guide assumes some familiarity with ESPHome.

* Pull this repository or copy the example configuration [`panasonic.yaml`](panasonic.yaml) from the root folder
* Uncomment the `type` field depending on which AC protocol you want to use
* Adjust the `panasonic.yaml` to your needs
* Connect your ESP
* Run `esphome run panasonic.yaml` and choose your serial port (or do this via the Home Assistant UI)
* If you see the handshake messages being sent (DNSK-P11) or polling requests being sent (CZ-TACG1) in the log you are good to go
* Disconnect the ESP and continue with hardware installation

## Setting supported features

Since Panasonic ACs support different features you can comment out the lines at the bottom of your `panasonic.yaml`:

```
  # Enable as needed
  # eco_switch:
  #   name: Panasonic AC Eco Switch
  # nanoex_switch:
  #   name: Panasonic AC NanoeX Switch
  # mild_dry_switch:
  #   name: Panasonic AC Mild Dry Switch
  # econavi_switch:
  #   name: Econavi switch
  # current_power_consumption:
  #   name: Panasonic AC Power Consumption
```

In order to find out which features are supported by your AC, check the remote that came with it. Please note that eco switch and mild dry switch are not supported on DNSK-P11.

**Enabling unsupported features can lead to undefined behavior and may damage your AC. Make sure to check your remote or manual first.**
**current_power_consumption is just as ESTIMATED value by the AC**

## Setting temperature offsets

As the internal sensors reading might not reflect the actual temperature in the room or outside, you can optionally define a fixed offset for both sensors.
This offset is internally applied to both, the reported temperature in ESPHome/HomeAssistant as well as to the target temperature. Any shown values always include the defined offset.

- Temperature readings of the AC will be reported as (reading + offset)
- Target temperatures will be set as (target - offset)
- An external `current_temperature_sensor` is reported as is, without the offset

```
    # Adapt according to your measurements
    current_temperature_offset: 0 
    outside_temperature_offset: 0
```

Examples:
- If the temperature is actually higher than measured by the AC, set the difference as a positive offset.
  - E.g. actual temperature = 23°, AC measured temperature = 20° --> offset = 3°
- If the temperature is actually lower than measured by the AC, set the difference as a negative offset.
  - E.g. actual temperature = 20°, AC measured temperature = 22° --> offset = -2°

## Polling (CZ-TACG1/cnt only)

**Why:** The CN-CNT protocol has no notifications. The ESP has to ask the AC for its state (poll) to see changes, for example from the IR remote, and to update the outside temperature and power sensors. Polling every few seconds around the clock is rarely needed, so the interval adapts: fast while something is happening, slow otherwise.

**How it works:**
- The fast interval is used after boot, after every command sent from Home Assistant, and when the AC reports a changed setting (e.g. the IR remote was used). It stays fast for `fast_poll_window`.
- After that the slow interval is used.
- Before every command the ESP polls once more, so a command is always based on the AC's current state (see [IR remote and Home Assistant together](#ir-remote-and-home-assistant-together-cz-tacg1cnt-only)).

```yaml
    poll_interval_fast: 5s    # default 5s, min 1s
    poll_interval_slow: 30s   # default: same as poll_interval_fast (constant rate as before), must be >= fast, max 10min
    fast_poll_window: 60s     # default 60s
```

**Examples:**
- Leave all three out: the AC is polled every 5 s as in earlier versions (plus one extra poll right before each command).
- `poll_interval_slow: 30s`: polls every 30 s when nothing happens, every 5 s for a minute after a change. Outside temperature and power consumption then update at least every 30 s.

## IR remote and Home Assistant together (CZ-TACG1/cnt only)

Every command on CN-CNT carries the complete state of the AC (mode, power, temperature, fan, vanes, presets). The ESP therefore polls right before sending, and only applies the settings you set in Home Assistant on top of the AC's current state. Settings made with the IR remote in the meantime are kept, and a value set in Home Assistant always applies, also when it equals the value the AC had before the remote was used.

Earlier versions built the command from the last poll. A change made with the IR remote shortly before a command from Home Assistant could be reverted, which could for example switch the AC off again.

## Simulated swing (CZ-TACG1/cnt only)

**Why:** With the airflow direction set to `auto`, many Panasonic units do not swing in heating mode. According to the service manual of e.g. the CS-HZ25TKE, the vertical vane is then held at a fixed angle chosen from the temperature of the indoor heat exchanger (coil), and the horizontal vane only swings once the coil is above about 43 °C. In cooling, fan and dry mode both vanes swing. Background: [docs/swing.md](docs/swing.md).

**What it does:** The select option `simulated_swing` makes ESPHome step a vane back and forth through fixed positions, in every operating mode. The AC's own options `auto` and `swing` are unchanged and can still be chosen.

- The vane moves one position every `vertical_swing_interval` / `horizontal_swing_interval`, in the order the positions are listed, and turns around at the ends of the list.
- Choosing another option in the select, changing the vane with the IR remote, or (with `climate_swing: native`) choosing a swing mode in the climate entity ends the simulation.
- While the AC is off, or not answering, the simulation pauses and continues afterwards.
- An active simulation is resumed after a restart of the ESP.
- The select shows `simulated_swing` while active, so the individual steps do not fill the Home Assistant history.
- The steps do not trigger fast polling.

```yaml
    vertical_swing_interval: 10s      # time between vertical vane steps, default 10s, 2s to 10min
    horizontal_swing_interval: 10s    # time between horizontal vane steps, default 10s, 2s to 10min
    vertical_swing_positions: [up, up_center, center, down_center, down]          # default: all 5
    horizontal_swing_positions: [left, left_center, center, right_center, right]  # default: all 5
    climate_swing: native             # native (default) or simulated
```

`climate_swing` decides what the swing modes of the climate entity (`vertical`, `horizontal`, `both`, `off`) do:
- `native`: the AC's own auto airflow direction, as in earlier versions.
- `simulated`: the simulated swing. `vertical` or `horizontal` also stops the simulation of the other vane. `off` stops both and returns the vanes to the positions they had before.

**Example: spread heat in a holiday home before arrival.** Keep the vertical vane fixed, since warm air rises and blowing upwards heats the ceiling, and sweep the horizontal vane across the room:

```yaml
climate:
  - platform: panasonic_ac
    type: cnt
    name: Panasonic AC
    horizontal_swing_interval: 15s
    horizontal_swing_positions: [left, left_center, center, right_center, right]
    vertical_swing_select:
      name: Panasonic AC Vertical Swing Mode
    horizontal_swing_select:
      name: Panasonic AC Horizontal Swing Mode
```

```yaml
# Home Assistant automation actions
- action: select.select_option
  target:
    entity_id: select.panasonic_ac_vertical_swing_mode
  data:
    option: center
- action: select.select_option
  target:
    entity_id: select.panasonic_ac_horizontal_swing_mode
  data:
    option: simulated_swing
```

**Example: limit the vertical sweep** to the middle positions, e.g. to avoid blowing straight down onto furniture:

```yaml
    vertical_swing_positions: [up_center, center, down_center]
```

Whether the indoor unit beeps on every command received via CN-CNT depends on the model. Test this by changing a fixed vane position in Home Assistant a few times before using short intervals.

## Communication loss (CZ-TACG1/cnt only)

If the AC has not answered for three times the slow poll interval plus 2 s (17 s with the defaults), the ESP logs a warning and sets the component warning status (a configured `status_led` blinks). The outside temperature, the power consumption and the current temperature (unless an external `current_temperature_sensor` is used) are reported as unknown. Everything is updated again with the next valid answer.

# Hardware installation

[Hardware installation for DNSK-P11](README.DNSKP11.md)

[Hardware installation for CZ-TACG1](README.CZTACG1.md)
