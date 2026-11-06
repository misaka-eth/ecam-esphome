# ecam-esphome

ESPHome firmware for DeLonghi ECAM coffee machines (tested on an Eletta Explore) that replaces the
stock Wi-Fi module with an M5Stack Atom Lite on the mainboard UART.

## Wiring (J1, 2.0 mm header)

| J1 | Atom Lite |
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

State, progress, alarms, door/container switches, counters (water, coffees, teas, milk cleans),
power/standby/stop buttons and a `brew` API action. The machine's Wi-Fi icon shows the Atom's real
Wi-Fi signal and API link.
