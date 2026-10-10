# Vane control and simulated swing

[Back to README](../README.md)

This page explains how a Panasonic indoor unit controls its vanes, why the AC's own `auto` airflow direction often does not swing in heating mode, and how `simulated_swing` works around that.

The figures below come from the service manual of the CS-HZ25TKE / CS-HZ35TKE / CS-HZ25TKE-5 (Panasonic order no. PAPAMY1707099CE, section 14.4). Other models may differ in details, but owners of related models report the same behaviour.

## Two independent settings

- **Operating mode:** heat, cool, fan, dry (and auto).
- **Airflow direction**, set separately for each vane: `auto`, or one of five fixed positions. Some models also have a vertical `swing`.

The vertical airflow is set by the horizontal vane (up/down), the horizontal airflow by the vertical vanes (left/right). In this component they are called the vertical and horizontal swing.

## Airflow direction `auto`

| Operating mode | Vertical (up/down) | Horizontal (left/right) |
|---|---|---|
| Cool, fan, dry | Swings 45° to 70° (upper vane) | Swings 65° to 115° |
| Heat | **Does not swing.** Held at a fixed angle chosen from the indoor heat exchanger (coil) temperature: 25° below about 30 to 36 °C, 55° between about 36 and 50 °C, 25° above about 58 °C | Swings only when the coil is above about 43 °C, otherwise fixed straight ahead (90°) |

All temperatures in this table are the temperature of the **indoor heat exchanger (coil)**, not the room air. In heating mode the coil is typically at 35 to 55 °C. The room temperature only has an indirect effect: a higher set temperature makes the compressor run harder, which makes the coil hotter.

## Fixed positions

With a fixed position the vane stays where it is set, in every operating mode. The angle of each position differs slightly between heating and cooling.

- **Vertical**, upper vane angle in heating mode for manual positions 1 to 5: 20°, 45°, 55°, 65°, 70°. Assuming the select options `up` to `down` map to positions 1 to 5 in order (the protocol codes 0x1 to 0x5 suggest so, but this is not confirmed by the manual), `center` is 55°.
- **Horizontal**, the five patterns cover 65° to 115°, with 90° straight ahead. Which pattern is left and which is right is not stated in the manual.

If the mapping above is right, `auto` in heating mode with a warm coil (55°) gives the same vertical angle as the fixed position `center`.

## Why Panasonic does not swing in heating mode

Panasonic does not state the reason. The control logic and Panasonic's own advice point to:

1. **No draft from lukewarm air.** While the coil is still cool (after start, after defrosting, at low compressor output) the outlet air is only lukewarm. The vane then blows almost horizontally, away from people. A swinging vane would repeatedly sweep lukewarm air over the occupants. The unit has a separate cold draft prevention in heating mode, which is only disabled in +8/15 °C heat operation.
2. **Warm air rises.** When swinging upwards, warm air ends up at the ceiling and the floor stays cold. Panasonic recommends directing the air downwards in heating mode.

These reasons matter for occupied rooms. When heating an empty house, for example a holiday home before arrival, spreading the heat across the room with a horizontal sweep is often more useful.

## How `simulated_swing` works

ESPHome steps the vane through the configured fixed positions and back, one step every `*_swing_interval`:

```
left -> left_center -> center -> right_center -> right -> right_center -> center -> ...
```

- Each step is a normal command on CN-CNT. Like every command it is built on a fresh poll of the AC, so only that vane changes.
- The simulation ends when another option is chosen in the select, or when the vane is changed with the IR remote (detected on the next poll).
- It pauses while the AC is off and resumes after a restart of the ESP.
- The select shows `simulated_swing` while active. The individual positions are not published, so the Home Assistant history is not filled with steps.

Recommended use for heating: set the vertical select to a fixed `center` or `down_center`, and the horizontal select to `simulated_swing`:

```yaml
    horizontal_swing_interval: 10s
    horizontal_swing_positions: [left, left_center, center, right_center, right]
```

If the vertical vane should swing as well, limit it to the middle positions so warm air is not blown towards the ceiling:

```yaml
    vertical_swing_positions: [up_center, center, down_center]
```

## Protocol codes

Byte 4 of the CN-CNT data holds both vanes, the vertical one in the high nibble and the horizontal one in the low nibble.

| Vertical | Code | Horizontal | Code |
|---|---|---|---|
| `auto` | 0xF | `auto` | 0xD |
| `swing` | 0xE | | |
| `up` | 0x1 | `left` | 0x9 |
| `up_center` | 0x2 | `left_center` | 0xA |
| `center` | 0x3 | `center` | 0x6 |
| `down_center` | 0x4 | `right_center` | 0xB |
| `down` | 0x5 | `right` | 0xC |

Whether a specific model supports vertical `swing` (0xE) is unknown. The CS-HZ25TKE manuals only mention `auto` and the five fixed positions. `simulated_swing` only uses the fixed positions and works on every model that supports them.

## Sources

- Panasonic service manual CS-HZ25TKE / CS-HZ35TKE / CS-HZ25TKE-5, order no. PAPAMY1707099CE, sections 14.4 (Airflow Direction) and 14.12 (+8/15 °C Heat Operation): https://jseducation.se/files/pages/papamy1707099ce.pdf
- Panasonic UP LIFE, airflow direction in heating mode (Japanese, March 2025): https://panasonic.jp/life/air/170114.html
- Mitsubishi Electric heat pump guide, airflow direction: https://www.mitsubishi-electric.co.nz/materials/aircon/info_guides/heat-pump-guide-gs-series.pdf
- Owner report for CS-HZ25UKE (Swedish, March 2020): https://www.byggahus.se/forum/threads/problem-med-ny-luftvarmepump.369445/
- Panasonic Comfort Cloud API values for the vanes (pcomfortcloud): https://github.com/lostfields/python-panasonic-comfort-cloud
