#!/usr/bin/env python3
"""Read the coffee Atom (ESPHome native API) state: ecam_esp.py [seconds]."""
import asyncio
import os
import sys
from pathlib import Path

from aioesphomeapi import APIClient


def load_env(path=Path.home() / ".config/ecam/atom.env"):
    env = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip()
    return env


async def main(seconds):
    env = load_env()
    cli = APIClient(env["ECAM_ADDRESS"], 6053, None, noise_psk=env["ECAM_API_KEY"], client_info="ecam-esp")
    await cli.connect(login=True)
    info = await cli.device_info()
    print(f"# {info.name} esphome {info.esphome_version} mac {info.mac_address}")
    entities, _ = await cli.list_entities_services()
    names = {e.key: e.name for e in entities}
    seen = {}

    def on_state(s):
        name = names.get(s.key, s.key)
        val = getattr(s, "state", None)
        if getattr(s, "missing_state", False):
            val = "unknown"
        if seen.get(name) != val:
            seen[name] = val
            print(f"{name}: {val}", flush=True)

    cli.subscribe_states(on_state)
    await asyncio.sleep(seconds)
    await cli.disconnect()


asyncio.run(main(float(sys.argv[1]) if len(sys.argv) > 1 else 4))
