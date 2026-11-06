# ECAM UART protocol (Eletta Explore, ECAM v2.1)

Reverse-engineered by sniffing the stock Wi-Fi module and the De'Longhi app, then replaying frames
without the module. Names follow the community ECAM work where it matches
([mmastrac/longshot](https://github.com/mmastrac/longshot),
[manekinekko/cafy](https://github.com/manekinekko/cafy)); this machine differs from their BLE
models in the brew frame, so treat everything not marked as confirmed with care.

`tools/ecam.py` implements all of this (CLI plus a daemon owning the tty);
`components/ecam/` is the ESPHome version.

## Framing

```
request  0D LEN payload CRC16
answer   D0 LEN payload CRC16
```

- `LEN` = number of bytes after the first one, CRC included (`len(payload) + 3`).
- `CRC16` = CCITT poly 0x1021, **init 0x1D0F**, big-endian, over everything before it (from `0D`/`D0`).
- The answer echoes the command byte(s), so answers are matched by `payload[0]`.
- One request in flight at a time. The stock module polls the monitor about once a second.
- The mainboard also emits an unsolicited `0B 09 E9 BF ...` frame about once a second when no module
  answers; its meaning is unknown and it can be ignored.

## Commands

| Request | Meaning | Answer |
|---------|---------|--------|
| `75 0F` | monitor (status) | `D0 12 75 0F` + 13 bytes, see below |
| `84 0F 02 01` | power on (from standby; cold start runs a rinse) | |
| `84 0F 01 01` | standby | |
| `84 0F 03 02` | sent by the app when it opens; not needed | |
| `83 F0 <bev> <op> <TLV...> <tail>` | recipe save / brew / stop, see below | `83 F0 01 00` accepted |
| `95 0F <pid u16> 01` | read parameter | `95 0F <pid> <u32>` |
| `90 0F <pid u16> <u32>` | write parameter | `90 0F <pid> 00` |
| `A1 0F <pid u16> <n>` | read n parameters from pid | values only |
| `A2 0F <start u16> <n>` | statistics: next n counters with id ≥ start | n × (`id u16`, `u32`) |
| `A4 F0 <slot> <slot>` / `A5 F0 <slot> <icon> <name UTF-16BE 20 B> <mug>` | read / write profile | |
| `A9 F0 <slot>` | select active profile (read: `A9 F0`, last byte) | |
| `A6 F0 <profile> <bev>` | read stored recipe | TLV |
| `AA F0 <slot> <n>` / `AB F0 <slot> 01 <name UTF-16BE 20 B> <image>` | read / write custom drink name | |
| `AC F0 <profile>` / `AD F0 <profile> <11 bev ids>` | read / write favourites (first = newest, `00` = empty) | write ACK `00 01` |
| `B0 F0 <n>` | drink limits (min/default/max per tag) | |
| `B9 F0 <slot>` / `BB F0 <slot> <name 40 B> <grind> <temp> <aroma> <blend> <roast>` | select / write Bean Adapt slot | |
| `ED F0 <icon>` | Wi-Fi icon on the display, see below | |

Deleting a profile is a rename to an all-zero name. Deleting a custom drink: `83 F0 <E6..EB> 00 ... 04`
(ACK `00 01`).

## Monitor `75 0F`

| Byte | Meaning |
|------|---------|
| 0 | accessory: 00 none, 01 hot-water spout, 02 milk jug, 04 milk jug (clean position), 06 cold-milk jug |
| 1–2 | **switches**, u16 little-endian (below) |
| 3–4 | **alarms**, u16 little-endian (below) |
| 5 | state: 00 standby, 01 heating, 02 turning off, 04 descaling, 07 ready / brewing, 08 rinsing, 0A milk, 0B hot water, 0C milk clean |
| 6 | phase within the state (coffee brews inside state 07 with a non-zero phase) |
| 7 | progress, 0–100 % |
| 8–12 | not decoded |

Switches (physical sensors; the display's "insert …" prompts come from here, not from alarms):
bit 0 spout, 1 brew-unit motor up, 2 brew-unit motor down, **3 grounds container out**, **4 water tank
out**, 5 knob, 6 water low, 7 unknown (set in normal operation), 8 jug, 10 clean knob, 13 door open,
14 pre-ground door open. Bits 3 and 4 are confirmed on this machine.

Alarms: bit 0 water tank empty, **1 grounds container full** (confirmed), 2 descale, 3 replace filter,
4 grind too fine, **5 beans empty** (confirmed), 6 service, 7 heater probe, 8 too much coffee,
9 brew-unit motor, 10 steamer probe, **11 empty drip tray**, 12 hydraulic problem, 13 insert tank,
14 clean knob, 15 beans empty.

Boiler temperature and flow are **not** exposed: the service menu shows them, but no read
(parameters 0–320 and 990–1060 scanned at ~82 °C and ~75 °C) changes with them.

## Brew frame `83 F0`

```
83 F0 <bev> <op> <TLV ...> <tail>
```

- `op`: `00` save the recipe without brewing, `02` stop, `03` start.
- `tail`: `05` save, `06` prepare (start / stop), `04` delete.
- TLVs are `tag value`; tags `01`, `09`, `0F` carry a u16 big-endian, the rest a u8.

| Tag | Meaning |
|-----|---------|
| `01` | coffee ml (u16) |
| `02` | strength 1 extra light … 5 extra strong (0 = pre-ground) |
| `08` | `08 00` appears on coffee starts from the app; not required |
| `09` | milk, u16 in 0.1 s (0x78 = 12.0 s) |
| `0B` / `0C` | milk froth / inversion (milk drinks) |
| `0D` | water temperature 0 low … 3 max (tea / hot water) |
| `0F` | water ml (u16) |
| `19` | visible in menu |
| `1B` | index length: `01` with max temperature, `04` otherwise |
| `1C` | accessory (01 spout, 02 jug) |
| `27` | `27 01` on start, `27 00` on save (sent by the app; not required) |

A minimal start is enough, e.g. filter-style 330 ml at strength 4:
`83 F0 03 03 01 01 4A 02 04 06`. The machine answers `01 00` when it accepts the order; only send
it in state 07 with no brew running. To-go drinks (`50`–`56`) start with no TLVs at all
(`83 F0 50 03 06`) and take size and recipe from the active profile.

Drink ids confirmed here: `01` espresso, `03` long / filter style, `06` americano, `0A` flat white,
`0C` hot milk, `10` hot water, `16` tea, `37` cold milk, `50` americano to go, `C8` espresso (Bean
Adapt), `E6`–`EB` custom slots 1–6. `tools/ecam.py` lists the other usual ECAM ids (unverified).

## Parameters (`95` / `90`)

The service menu shows the same ids as `PAR <decimal>`.

| PID (dec / hex) | Meaning |
|-----------------|---------|
| 50 / 0x32 | water hardness; display shows value + 1 |
| 61 / 0x3D | coffee temperature 0 low, 1 medium, 2 high |
| 62 / 0x3E | auto-off: 0 15 min, 1 30 min, 2 1 h, 3 3 h |
| 63 / 0x3F | flags: bit 0 autostart **off**, 1 12-hour clock, 2 key beep, 3 cup light, 4 energy saving, 7 water filter |
| 64–65 / 0x40–0x41 | autostart hour / minute |
| 106 | total water: display litres; raw value ×10 / 25 = ml |
| 400, 402 | flow-meter pulses, about 25 per ml (2620 pulses weighed as 105 g) |
| 115 | milk cleans |
| 199 | mainboard EEPROM version |
| 200–203 | production date |
| 205–209 | serial number (ASCII) |
| 210 | PIN |
| 3000 / 3001 / 3002 / 3003 | category totals: coffee only / with milk coffees / water drinks (hot water + tea) / to-go |
| 3003 + bev id | per-drink counters: 3004 espresso (01), 3013 flat white (0A), 3015 hot milk (0C), 3019 hot water (10), 3025 tea (16) … |

Write bit flags read-modify-write; a wrong full-word write changes several settings at once.
The Statistics screen's "Total with hot milk" is PAR 3001 plus hot milk alone (3015); "Total teas" is 3025.
Descale and filter counters were not found (PAR 105, 108, 116 stay 0).

Service menu reading on this machine (for reference): PAR 400 = PAR 402 = 221680, PAR 401/403/404/405 = 0,
PAR 3000 = 15, 3001 = 7, 3002 = 15, 3003 = 1, 3025 = 7, PAR 100 = 0, 105 = 0, 106 = 7, 108 = 0,
115 = 11, 116 = 0, PAR 50 = 4.

## Wi-Fi icon `ED F0 3x`

The module reports its link state; the display draws an icon from it and the mainboard does not
check anything.

| Value | Icon |
|-------|------|
| 30 | none |
| 31 | 4 bars |
| 32 | cloud crossed out |
| 33 | 3 bars |
| 34 | 1 bar |
| 35 | grey, 0 bars |
| 36 | 2 red bars |
| ≤ 2F, ≥ 37 | ignored |

The firmware sends it every 5 s from its real RSSI and API connection.

## Behaviour worth knowing

- A cold power-on rinses about 20 s through the coffee spout (≈70 ml), so an unattended morning brew
  needs a cup already under the spout.
- The machine refuses a brew while another runs or outside state 07; the firmware only sends one
  attempt.
