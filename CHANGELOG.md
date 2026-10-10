# Changelog

## Unreleased (branch configurable-poll-and-swing)

Changes for CZ-TACG1 (`type: cnt`) unless noted otherwise.

### Added
- Adaptive polling with `poll_interval_fast`, `poll_interval_slow` and `fast_poll_window`. Without these options the AC is polled every 5 s as before.
- Simulated swing: the option `simulated_swing` in both swing selects steps the vane through fixed positions in every operating mode. Configured with `vertical_swing_interval`, `horizontal_swing_interval`, `vertical_swing_positions`, `horizontal_swing_positions` and `climate_swing`. See [docs/swing.md](docs/swing.md).
- Warning status and unknown sensor values when the AC stops answering.
- Config validation of the uart settings (9600 baud, 8E1), for both `cnt` and `wlan`.

### Fixed
- Commands no longer revert settings made with the IR remote. A command is now built on the AC's state from a poll made right before sending, and only the values set in Home Assistant are applied on top of it. Previously this could for example switch the AC off again.
- The econavi switch no longer clears Powerful/Quiet and nanoeX, and the nanoeX switch no longer clears econavi.
- Back-to-back packets from the AC are no longer merged and dropped.
- Short packets no longer cause out-of-bounds reads.
- Estimated power consumption no longer wraps around to a negative or huge value.
- Vertical `swing` is shown as vertical swing in the climate entity instead of off.
- Swing positions without a select option no longer log an error on every poll.
- The climate action shows idle instead of heating/cooling when the AC reports 0 W.

### Changed
- The example configuration `ac.yaml.example` is renamed to [`panasonic.yaml`](panasonic.yaml) and points to this branch.
- The climate state is only sent to Home Assistant when something changed, instead of on every poll.
- `current_temperature_offset` is no longer applied to an external `current_temperature_sensor`. **Check your configuration** if you use both: the offset now only compensates the AC's own sensor (and the target temperature).
- A wrong uart configuration (e.g. missing `parity: EVEN`) now fails config validation instead of compiling and getting no answer from the AC.
- README: the minimum ESPHome version is corrected to 2026.4.0. The code already required it before these changes (`set_supported_custom_fan_modes` on the climate entity).
