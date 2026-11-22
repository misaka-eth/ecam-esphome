# ecam-esphome

ESPHome firmware for DeLonghi ECAM coffee machines (tested on an Eletta Explore) that replaces the
stock Wi-Fi module with an M5Stack Atom Lite on the mainboard UART.

## Wiring (mainboard J8, 2.0 mm header)

Pin numbers as on the stock module's J1A connector; details and photos in
[docs/hardware.md](docs/hardware.md).

| J8 | Atom Lite |
|----|-----------|
| 4 (3V3) | 3V3 |
| 8 (GND) | GND |
| 5 (machine TX) | G19 (RX) |
| 6 (machine RX) | G22 (TX) |

## Use

Copy `example.yaml` into the ESPHome dashboard as your device file and add the keys from
`secrets.example.yaml` to the dashboard's `secrets.yaml`. The device file pulls
`ecam/coffee-machine.yaml` and the `ecam` component from this repo, so updates need no file copying.

## What it exposes

- State, progress, alarms, door/container switches, water total and milk cleans.
- Statistics: a counter per drink category and per drink (`A2` counters; unused drinks read 0).
- Power/standby/stop buttons, a `Restart` button for the ESP, a `Drink` select with a `Brew drink` button, a `brew` API action and
  the machine's answer to the last brew (`Last brew`: accepted, or rejected with the answer's bytes).
- Settings, including the ones the app hides for some models: coffee temperature, auto-off, water
  hardness, autostart (on/off, hour, minute), 12-hour clock, key beep, cup light, energy saving, water filter.
- Profiles: active profile, editable profile names; favourites and custom drink names of the
  active profile.
- API actions for stored recipes (`read_recipe`, `save_recipe`, answer in the `Recipe` sensor),
  `set_favorites`, `delete_custom_drink`, `select_bean_adapt` and `raw`.
- Diagnostics: serial number, production date, EEPROM version, PIN (disabled by default).
- The machine's Wi-Fi icon shows the Atom's real Wi-Fi signal and API link.

- A recipe editor: choosing a drink in `Drink` loads its stored recipe (active profile) into
  `Recipe coffee / water / milk / strength / water temperature`; `Save recipe` writes it back.

A setting the machine does not have is ignored or reads back unchanged. `select_bean_adapt` follows the
app's frame but is not confirmed on a machine yet.

## Reverse-engineering notes

- [docs/hardware.md](docs/hardware.md): boards, pinout, why the stock module is not reflashable, photos
- [docs/protocol.md](docs/protocol.md): framing, commands, monitor, brew frame, parameters, Wi-Fi icon
- [tools/](tools/): `ecam.py` (CLI + daemon over a USB-UART adapter), `ecam_dump.sh` (dump every
  known read for diffing), `ecam_esp.py` (read the firmware's state over the ESPHome API)
