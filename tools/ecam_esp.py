#!/usr/bin/env python3
"""Talk to the coffee Atom over the ESPHome native API.

  ecam_esp.py [seconds]             print entity states (default 4 s)
  ecam_esp.py raw <hex> [...]       send raw ECAM payloads via the firmware's `raw` action, print answers
  ecam_esp.py stats                 walk all statistics counters (A2 0F) and print id=value

Reads ECAM_ADDRESS / ECAM_API_KEY from ~/.config/ecam/atom.env.
"""
import asyncio
import re
import sys
from pathlib import Path

from aioesphomeapi import APIClient, LogLevel


def load_env(path=Path.home() / ".config/ecam/atom.env"):
    env = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip()
    return env


async def connect():
    env = load_env()
    cli = APIClient(env["ECAM_ADDRESS"], 6053, None, noise_psk=env["ECAM_API_KEY"], client_info="ecam-esp")
    await cli.connect(login=True)
    return cli


async def states(seconds):
    cli = await connect()
    info = await cli.device_info()
    print(f"# {info.name} esphome {info.esphome_version}")
    entities, _ = await cli.list_entities_services()
    names = {e.key: e.name for e in entities}
    seen = {}

    def on_state(s):
        name = names.get(s.key, s.key)
        val = "unknown" if getattr(s, "missing_state", False) else getattr(s, "state", None)
        if seen.get(name) != val:
            seen[name] = val
            print(f"{name}: {val}", flush=True)

    cli.subscribe_states(on_state)
    await asyncio.sleep(seconds)
    await cli.disconnect()


class Raw:
    """Calls the `raw` action and collects the `raw <tx> -> <rx>` log lines."""

    LINE = re.compile(r"raw ([0-9a-fA-F.]+) -> ([0-9a-fA-F.]+|no answer)")

    def __init__(self, cli, service):
        self.cli, self.service = cli, service
        self.answers = asyncio.Queue()

    def on_log(self, msg):
        text = msg.message.decode(errors="replace") if isinstance(msg.message, bytes) else msg.message
        m = self.LINE.search(re.sub(r"\x1b\[[0-9;]*m", "", text))
        if m:
            rx = None if m.group(2) == "no answer" else bytes.fromhex(m.group(2).replace(".", ""))
            self.answers.put_nowait(rx)

    async def __call__(self, hexstr, timeout=3.0):
        while not self.answers.empty():
            self.answers.get_nowait()
        await self.cli.execute_service(self.service, {"payload": hexstr})
        try:
            return await asyncio.wait_for(self.answers.get(), timeout)
        except asyncio.TimeoutError:
            return None


async def raw_session():
    cli = await connect()
    _, services = await cli.list_entities_services()
    service = next((s for s in services if s.name == "raw"), None)
    if service is None:
        sys.exit("firmware has no `raw` action (update it)")
    raw = Raw(cli, service)
    cli.subscribe_logs(raw.on_log, log_level=LogLevel.LOG_LEVEL_INFO)
    await asyncio.sleep(0.3)
    return cli, raw


async def raw_cmd(payloads):
    cli, raw = await raw_session()
    for p in payloads:
        rx = await raw(p)
        print(f"TX {p}  RX {rx.hex(' ') if rx else '-'}")
    await cli.disconnect()


async def stats_cmd():
    """A2 0F <start u16> <n> answers n x (id u16, value u32) with id >= start."""
    cli, raw = await raw_session()
    start, out = 0, {}
    while start <= 0xFFFF:
        rx = await raw(f"a20f{start:04x}08")  # 8 entries: the answer must fit the firmware's 64-byte frame buffer
        if not rx or len(rx) < 6:
            break
        body = rx[4:-2]
        if len(body) < 6:
            break
        last = start
        for i in range(0, len(body) - 5, 6):
            pid = int.from_bytes(body[i:i + 2], "big")
            out[pid] = int.from_bytes(body[i + 2:i + 6], "big")
            last = pid
        if last < start:
            break
        start = last + 1
    await cli.disconnect()
    for pid in sorted(out):
        print(f"{pid}={out[pid]}")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "raw":
        asyncio.run(raw_cmd(a[1:]))
    elif a and a[0] == "stats":
        asyncio.run(stats_cmd())
    else:
        asyncio.run(states(float(a[0]) if a else 4))
