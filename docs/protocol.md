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
| `95 0F <pid u16> <n>` | read n consecutive parameters (n up to 48 tried) | `95 0F <pid> <u32> × n` |
| `90 0F <pid u16> <u32>` | write parameter | `90 0F <pid> 00` |
| `A1 0F <pid u16> <n>` | read n parameters from pid | values only |
| `A2 0F <start u16> <n>` | statistics: next n counters with id ≥ start | n × (`id u16`, `u32`) |
| `A4 F0 <first> <count>` / `A5 F0 <first> <last> (<name UTF-16BE 20 B> <icon> <mug>)…` | read / write profiles | the machine answers `A4 F0 01 04` but not a read of one profile (`A4 F0 04 01`); A5 answers `00` ok, `01` rejected (an icon of `FF`, as unset profiles read, is rejected) |
| `A9 F0 <slot>` | select active profile (read: `A9 F0`, last byte) | in standby the machine acknowledges it but keeps the old profile |
| `A6 F0 <profile> <bev>` | read stored recipe | TLV |
| `AA F0 <slot> <n>` / `AB F0 <slot> 01 <name UTF-16BE 20 B> <image>` | read / write custom drink name | |
| `AC F0 <profile>` / `AD F0 <profile> <11 bev ids>` | read / write favourites (first = newest, `00` = empty) | write ACK `00 01` |
| `B0 F0 <n>` | drink limits (min/default/max per tag) | |
| `B9 F0 <slot>` / `BB F0 <slot> <name 40 B> <grind> <temp> <aroma> 00 <set up>` | select / write Bean Adapt slot (app: BeanSystem; grind in half steps 2..14 = 1.0..7.0, temperature 0..3 low/mid/high/very high, aroma 0..5 pre-ground/extra mild/mild/normal/strong/extra strong, set up 1 = slot in use) | |
| `ED F0 <icon>` | Wi-Fi icon on the display, see below | |
| `E2 F0 <hour> <minute>` | set the machine clock (app: `setHour`); not sent again after a refusal or no answer | `E2 F0 00` ok |
| `B0 F0 <bev>` | drink limits, see below | |
| `BA F0 <n>` | Bean Adapt slot read (longshot: BeanSystemRead), n = 1..6 (7: no answer) | `n`, name 20×UTF-16BE (NUL-terminated, old bytes may follow), grind, temperature, aroma, 00, set up (1), enabled — as `BB`; an unused slot reads all 0 and is named "Kaffeebohnen n" on a German machine. `B9 F0 n` answers `n 01` even for an unused slot; BA does not show which slot is active. The Bean Adapt drinks are C8..CD for slots 1..6 |
| `A3 F0` | recipe checksums (longshot: Checksum) | 6 × u16; on an ECAM450.76.T the first four are one per profile (only the profile with an edited recipe differs: `33F8 360B 360B 360B`), the last two unknown |
| `A8 F0 <profile>` | drink order (longshot: RecipePriorityRead) | `profile` then the drink ids in display order (43 on an ECAM450.76.T: all drinks of the model plus defined custom drinks); profiles without an own order answer only `profile`; without the profile byte the answer is one byte (`A7`) |
| `E8 F0 00` | app "readRemoteControl" (`ED 7C` in the app's bytes is the frame CRC) | the mainboard does not answer it (nor `E8 0F 00`): handled by the stock Wi-Fi module |

Answer layouts (confirmed on an ECAM450.76.T through the firmware; the profile and custom-drink lists
make answers up to ~140 bytes, so a reader must not cap frames at 64):

| Request | Answer payload |
|---------|----------------|
| `A9 F0` | active profile 1..4 in the last byte before the CRC |
| `A4 F0 01 04` | from byte 6: 4 × (name 20 B UTF-16BE, icon, mug) |
| `AA F0 01 06` | from byte 6: 6 × (name 20 B UTF-16BE, image); unused slots read "Rezept n" / `visible=0` |
| `AC F0 <profile>` | bytes 5..15: 11 drink ids, newest first, `00` = empty |
| `A6 F0 <profile> <bev>` | from byte 6: the recipe TLVs |
| `A2 0F <start> 0A` | from byte 4: up to 10 × (`id u16`, `u32`); page on with `start = last id + 1` |

`B0 F0 <bev>` answers from byte 5 with `(tag, min, default, max)` per recipe value; values are u16 for the
u16 tags (`01`, `09`, `0F`), else u8 (the app's `getDefaultMinMaxQty`).

Drink ids of the ECAM450.76.T from the app's `assets/MachinesModels.json` (decimal there): `01`–`0D`, `0F`
cappuccino reverse, `10` hot water, `16` tea, `17` coffee pot, `18` cortado, `1B` brew over ice, `32`–`38`
iced americano / cappuccino / latte macchiato / cappuccino mix / flat white / cold milk / caffelatte,
`50`–`56` mug americano / cappuccino / latte macchiato / caffelatte / cappuccino mix / flat white / hot milk
(the "to-go" category PAR 3003), `64`–`6B` mug brew over ice and mug iced drinks, `C8` Bean Adapt espresso,
`E6`–`EB` custom. The same file marks autostart, clock, cup light and cup warmer as off for one SKU of
the model and on for another.

Deleting a profile is a rename to an all-zero name. Deleting a custom drink: `83 F0 <E6..EB> 00 ... 04`
(ACK `00 01`).

## Monitor `75 0F`

| Byte | Meaning |
|------|---------|
| 0 | accessory: 00 none, 01 hot-water spout, 02 milk jug, 04 milk jug (clean position), 06 cold-milk jug, 07 cold-milk jug in the clean position (confirmed on an ECAM450.76; the app asks to clean the jug then, as for 04). In standby the 450.76 reports 07 and switch bit 15 whatever is attached (spout included), from ~10 s after switching off until power-on |
| 1–2 | **switches**, u16 little-endian (below) |
| 3–4 | **alarms**, u16 little-endian (below) |
| 5 | state: 00 standby, 01 heating, 02 turning off, 04 descaling, 07 ready / brewing, 08 rinsing, 0A milk, 0B hot water, 0C milk clean |
| 6 | phase within the state (coffee brews inside state 07 with a non-zero phase) |
| 7 | progress, 0–100 % |
| 8–9 | **alarms bits 16–31** (the app reads the alarms as bytes 3, 4, 8, 9; see below) |
| 10 | 01 for ~20 s after a brew was aborted (e.g. a milk drink stopping at the milk), else 00 |
| 11–12 | not decoded, always 00 so far |

Phases seen (byte 6, one sample per second): power-on in state 01 runs 01 → 02 → 05 (rinse, progress
0 → 100 in ~20 s) → 06; an espresso in state 07 runs 04 → 05 → 06 (grinding, switches bits 1/2 brew-unit motor)
→ 08 → 09 → 0A → 0B (extraction, progress 5 → 100 in ~15 s) → 0D → 0E → 00. The live counters (PAR 3000+)
went up at 0E, before the machine was ready again. Hot water (spout in, accessory 01, switch bit 0) runs in
state 0B: 02 → 03 (dispensing, progress 0 → 100 in ~22 s for 80 ml) → 04 → 06 → back to 07/00. Brewing it with
water_ml 80 instead of the stored 150 poured about 90 ml (PAR 106), so the brew overrides are honoured.
An americano brews like an espresso up to 0E, then adds the water in state 07: 7B (~6 s) → 7F (water, ~44 s)
→ 80 → 81 → 00.

A brew that runs out of beans waits in state 07 phase 10 (decimal 16; alarm bit 20) until the hopper is refilled. Sending
the stop frame (`83 F0 <drink> 02`) then continues the drink instead of cancelling it, like the button on
the machine (seen on an ECAM450.65.G). After Stop mid-extraction the monitor reports progress 100 with
byte 10 = `01`; the Progress sensor shows 0 then.

Skip rinse (`83 F0 <drink> 06 27 00`) targets the rinse before a drink. On an ECAM450.65.G the power-on rinse
(state 01, phase 05) accepts it with `01 00` but runs to the end and no drink starts; in standby it gets no
answer, while heating `01 01`. On an ECAM450.76 (2026-11-28, cold start) skip rinse during the power-on
rinse, and a brew sent in standby or while heating, all get `01 01` and change nothing: the power-on rinse
always runs (also after only 80 min off), and no rinse before a drink was ever seen on that machine.

Switches (physical sensors; the display's "insert …" prompts come from here, not from alarms):
**0 spout**, 1 brew-unit motor up, 2 brew-unit motor down, **3 grounds container out**, **4 water tank
out**, 5 knob, **6 water low**, 7 coffee jug (app name; set in normal operation here), 8 milk carafe, 9 chocolate tank,
10 clean knob, **11 cold milk jug**, 13 door open, 14 pre-ground door open, **15 cold milk jug in the clean
position** (app names, enum `m6.p`; 11 and 15 have none).
Confirmed on an ECAM450.76 (2026-11-28): 0 exactly while the hot-water spout is in (accessory 01); 6 set at the
end of a coffee while the display asked to fill the tank, cleared by the refill (no alarm bit with it); 11 exactly
while the cold-milk jug is attached (accessory 06), on and off; turned to the clean position it reads accessory 07
and bit 15 instead of 11 (bit 15 and 07 also show in every standby, see the accessory byte). Bit 7 comes on
2–4 s after the water tank is put back (with or without a refill; 8 of 10 times in a week), goes off when the
tank is taken out or water runs through the brew unit (a brew or the power-on rinse), but not with hot water.
The Switches sensor names 0, 3, 4, 6, 11 and 15 (15 not in standby) and
shows the others as bit numbers; 7 came on after a tank refill and stayed (about 2 min on a 450.65.G). On an ECAM450.65.G (no chocolate, no jug) bit 9 was set
exactly while a brew waited for beans, and bit 7 for about 2 min around a water tank refill.

Alarm names are the app's (enum `m6.l`, MonitorDataV2 builds a u32 from bytes 3, 4, 8, 9). The app hides
bits 7, 10, 13, 16, 20 and 21. Bit 16 ("tank too full") is set in normal operation; bit 20 ("not enough
coffee") is set while a brew waits for beans (seen: hopper ran out mid-brew, phase 10, progress 0). Bits 16+:
16 tank too full, 17 bean hopper absent, 18 grid presence, 19 infuser sense, 20 not enough coffee,
21 expansion comm, 22 expansion submodules, 23 grinder 1, 24 grinder 2, 25 condenser fan, 26 clock/BT comm,
27 SPI comm. Bit 26 stays set on an ECAM450.65.G, which has no clock module; the Alarms sensor leaves out 16
and 26.

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
- `tail`: `(profile << 2) | operation` as the app builds it: operation 0 delete, 1 save, 2 prepare,
  3 prepare and save (also 5–7 for milk "inversion" variants). So `04` / `05` / `06` are delete / save /
  prepare **for profile 1**; profile 2 prepares with `0A`.
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
| `1B` | index length: `01` with max temperature, `04` otherwise. Also changes from `01` to `04` when a recipe is saved with a custom amount, so it may be the cup-size preset (`04` = custom) |
| `1C` | accessory (01 spout, 02 jug) |
| `27` | RINSE (app enum): rinse before the drink; the app's "skip rinse" re-sends the drink with op `06` and `27 00` |

The app's tag enum (`TEMP(0)` … `RINSE(39)`) also names `03` grind, `04` blend, `05` infusion speed,
`06` pre-infusion, `07` crema, `0A` milk temperature, `0E` tea profile, `18` programmable,
`1A` visible in programming, `1F` iced, `20` mug size, `21` mug adjust, `25` ice cubes, `26` intensity.

A minimal start is enough, e.g. filter-style 330 ml at strength 4:
`83 F0 03 03 01 01 4A 02 04 06`. The machine answers `01 00` when it accepts the order; only send
it in state 07 with no brew running. To-go drinks (`50`–`56`) start with no TLVs at all
(`83 F0 50 03 06`) and take size and recipe from the active profile.

Milk drinks are stricter (confirmed on a cappuccino, 2026-11-22). With no TLVs (`83 F0 07 03 06`) the machine
uses its stored recipe and the drink runs. With TLVs it must get the app's set for the drink: the stored
recipe's tags, accessory `1C` included and froth `0B` left out (the froth knob is mechanical, and no
ECAM450.76 recipe in the app's MachinesModels.json lists it). `01 00 3B 09 00 E6 02 05 1B 04 1C 02` ran;
`01 00 19 09 00 50`, `01 00 3B` alone and the full stored recipe with `0B 02` were all accepted (`01 00`)
but stopped about 5 s into the milk: "clean knob" (alarm bit 14), then "steamer probe" (bit 10), phase 06
instead of 07, monitor byte 10 = `01`, no coffee. A normal milk phase is state 0A phase 01 (~12 s) → 04
(milk, progress up to ~66 %) → 07, then the coffee runs in state 07 like an espresso. Start code `02`
(START_PROGRAM, the app's second step) is refused with `01 01` when nothing is running; `03` is right.

A drink it could otherwise start but refuses gets `01 <code>` instead of `01 00`. The machine's own
screen says why when the drink is chosen there, but shows nothing for a remote start, and the
monitor (`75 0F`) stays as it was: state 07, no alarm, no water flag. The firmware logs the whole
answer frame (`brew 0x03: rejected -> d0...`) and publishes it as the `Last brew` text sensor
(`03 rejected 01 05`, the bytes after `83 F0`).

| Answer | Meaning |
|--------|---------|
| `01 00` | accepted, the drink starts |
| `01 05` | refused: too little water in the tank for this drink (observed once, 2026-11-12: filter style 330 ml with a low tank, frame `D0 07 83 F0 01 05 34 7C`; the screen says "too little water for this drink" for the same drink started there) |

Other codes are not known yet.

Saving a recipe (confirmed): re-send the stored recipe (`A6`) with the changed values, op `00`, the
app's `27 00` and tail `05`, e.g. `83 F0 01 00 08 00 01 00 3C 1B 01 02 04 19 01 27 00 05` for espresso
60 ml. Reading the recipe back shows the new values; the machine set `1B` to `04` by itself.

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
| 115 | milk cleans (**confirmed**: +1 per milk clean; 116 unchanged) |
| 199 | mainboard EEPROM version |
| 200–203 | factory date: 201 month, 202 day, 203 year (two digits); 200 unknown (523). An ECAM450.76 reads 10 3 22 = 2022-10-03, one week after the production date on the sticker behind the water tank (YYMMDD 220926 = 2022-09-26) |
| 205–209 | serial number (ASCII); after the product code's letters comes the production date YYMMDD, as on the sticker behind the water tank (217073DU **220926** J0030 = 2022-09-26) |
| 210 | PIN |
| 3000 / 3001 / 3002 / 3003 | category totals: coffee only / with milk coffees / water drinks (hot water + tea) / to-go |
| 3003 + bev id | per-drink counters: 3004 espresso (01), 3013 flat white (0A), 3015 hot milk (0C), 3019 hot water (10), 3025 tea (16) … |

Write bit flags read-modify-write; a wrong full-word write changes several settings at once.
The Statistics screen's "Total with hot milk" is PAR 3001 plus hot milk alone (3015); "Total teas" is 3025.
Maintenance parameters (ECAM450.76.T, found by a read scan of PAR 0–2999 and diffs around single
actions; misaka's machine reads 0 for most of them):

| PAR | Value seen | Meaning |
|-----|-----------|---------|
| 63 bit 7 | 0 → 1 | water filter set in the menu (**confirmed**); clearing it also resets PAR 109 to 0 |
| 108 | 9 → 10 | filters installed (**confirmed**: +1 when a filter is set in the menu) |
| 109 | 0 → 249 | water through the filter since it was set, unit as PAR 106 (**confirmed**: 0 while no filter was set, then grew exactly like 106) |
| 105 | 5 | descales (presumed) |
| 100 | 87134 | water since the last descale (presumed): grows by the sum of the flow meters PAR 400 + 401 (25 pulses/ml); unchanged by a filter change |
| 400, 401 | +3470/+20 espresso, +1729/+14 rinse, +4660/+3800 cappuccino + milk clean | two flow meters, 401 mainly the milk/steam circuit; 402/403 grow alike (lifetime totals) |
| 101 | + PAR 51 per brew, 0 per rinse | **grounds container fill** (confirmed: 0 after emptying the container, 2026-11-22). The machine can't tell whether it was emptied: taking the container fully out (19 s) reset 101, 40 and 41 without emptying, a short pull (5 s, switch bit 3 set as well) did not; about 0.39 g of wet grounds per unit (593 ≈ 230 g weighed) |
| 40 | 0 → 105 over 14 brews | **grounds container fill in coarser units** (confirmed): about PAR 101 / 5.7, 0 after emptying |
| 41 | 10 → 0 | **countdown to "grounds container full"** (confirmed 2026-11-27): ceil((≈98.5 − PAR 40) / ≈10.2) at all 15 points; the alarm (bit 1) came in the same frame as 0 (PAR 40 98 → 105), 10 after emptying. `Grounds container fill` = PAR 40 / `ecam_grounds_full` (99) in percent |
| 42, 43 | 10 → 12, 3 → 8 in five days | unknown; rise after a power-on rinse (43 once at a brew too), **not** reset by emptying the grounds container and drip tray |
| 505 | +1 per drink, 0 per rinse | unknown per-drink counter |
| 57 | 278 → 279 → 276 | unknown, not a counter |
| 51–56 | last two brews: 51/53/55 newest, 52/54/56 previous, unchanged in standby | 55 dose setpoint by strength (espresso 40 ml: strength 1 → 6700, 5 → 11400; 2x espresso 12800); 53 the measured result, 55/53 ≈ 280 (no scale in the machine, so presumably brew-unit travel or grinder count); 51 the grounds added to 101 (strength 1 → 23, 5 → 46 for a 40 ml espresso, but 13 for a 40 ml cappuccino at 5) |
| 502, 503 | values shift between brews | unknown |

Only PAR 0, 32–35, 40–58, 61–63, 71, 100–116, 194, 199–215, 400–407 and 500–505 are non-zero below 3000;
every id answers. Statistics (`A2`) also list blocks 23000–23009 (23004/23007/23008 +127 per espresso,
23001 +5, 23005 +125, 23006 +218 for a cappuccino with milk clean, 0 per rinse) and 43000–43016 (43005, 43010 +1 per drink). A power-on rinse changes only the water
values (100, 106, 109, 400–403).

The `A2` statistics lag: the machine writes them when it goes to standby, so drinks made since the last
power-on are missing until then (101 and 109 included). Reading the same ids as parameters
(`95 0F 0B B8 30` = 3000–3047) gives the same values, but live (confirmed: 3000/3004/3005 +1 at the end of an espresso);
the counter sensors use that. A 2x espresso from the display counts as two
espressos (3000, 3004 and 3005 +2, 3007 unchanged; 23001/23002 +8, 23004/23007/23008 +178, 43011 +2,
43012/43016 +1).

PAR 205–209 hold the serial number as 4 ASCII bytes each (big-endian, NUL/space padded). PAR 200–203 read
`523 10 3 22` on a machine built in autumn 2022: 201–203 look like month / day / year (2022-10-03),
200 is unknown. PAR 199 read `0xE00C`. Settings the app hides for a model (autostart, 12-hour clock,
cup light) still read and write on the ECAM450.76.T.

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
- A drink the tank has too little water for is refused at once (`01 05`), with no alarm and no flag
  in the monitor (see the brew frame above); only the brew answer tells.
