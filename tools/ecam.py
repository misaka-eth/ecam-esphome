#!/usr/bin/env python3
"""ecam — De'Longhi ECAM (Eletta Explore) control over the ESP UART (J1-5/J1-6), no ESP needed.

  ecam.py serve                     daemon: owns the tty, raw log, monitor poll, socket API
  ecam.py status [-w]               decoded 0x75 monitor (-w: follow changes)
  ecam.py on | off                  power on / standby
  ecam.py brew <drink> [--coffee ML] [--water ML] [--milk SEC] [--strength 1-5] [--temp low..max] [--wait]
  ecam.py stop [<drink>]            stop the running (or given) drink
  ecam.py drinks                    known drink ids
  ecam.py recipe <drink> [-p N]     stored recipe of profile N (default: active)
  ecam.py profiles                  profile slots + active one
  ecam.py rename <slot> <name>      rename profile slot
  ecam.py customs                   custom drink names (slots 1-6 = 0xE6-0xEB)
  ecam.py delete-custom <slot>      remove custom drink slot 1-6 from the menu
  ecam.py favorites [-p N]          favourite drinks of profile N (default: active)
  ecam.py favorites set [-p N] <drink> [...]   replace the list (max 11, first = newest); no drinks clears it
  ecam.py settings                  coffee temp, water hardness, flag bits of 0x3F
  ecam.py set temp|hardness|beep|eco <value>
  ecam.py param <pid> [<value>]     read / write a raw u32 parameter
  ecam.py stats                     statistics counters (0xA2)
  ecam.py raw <hex> [...]           send payload(s), print answers

Frame: 0D LEN payload CRC16 (CCITT, init 0x1D0F), LEN = len(payload)+3; answers start with D0.
Only the daemon touches the serial port; clients talk to it over $XDG_RUNTIME_DIR/ecam.sock.
Protocol notes: docs/protocol.md.
"""
import argparse, json, os, queue, socket, socketserver, sys, termios, threading, time

DEV = os.environ.get("ECAM_DEV", "/dev/ttyUSB0")
LOGDIR = os.path.expanduser(os.environ.get("ECAM_LOGDIR", "~/reverse"))
SOCK = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ecam.sock")

# Drink ids (bev). Confirmed on this machine: espresso, flat_white, hot_milk, hot_water,
# cold_milk, americano_togo, espresso_soul, custom1-6. The rest follow the usual ECAM
# numbering and are unverified; any id can also be given as hex (e.g. 0x07).
DRINKS = {
    "espresso": 0x01, "coffee": 0x02, "long": 0x03, "espresso2x": 0x04, "doppio": 0x05,
    "americano": 0x06, "cappuccino": 0x07, "latte_macchiato": 0x08, "caffe_latte": 0x09,
    "flat_white": 0x0A, "espresso_macchiato": 0x0B, "hot_milk": 0x0C, "cappuccino_doppio": 0x0D,
    "cappuccino_mix": 0x0F, "hot_water": 0x10, "tea": 0x16, "coffee_pot": 0x17, "cortado": 0x18,
    "long_black": 0x1B, "cold_milk": 0x37, "americano_togo": 0x50, "espresso_soul": 0xC8,
    **{f"custom{i}": 0xE5 + i for i in range(1, 7)},
}
NAMES = {v: k for k, v in DRINKS.items()}

# Recipe TLV tags: u16 tags listed here, everything else is u8.
TAG16 = {0x01, 0x09, 0x0F}
# ed f0 3x wifi icon: 30 none, 31 4 bars, 32 cloud off, 33 3 bars, 34 1 bar, 35 grey/0 bars, 36 2 red bars; 37+ ignored.
WIFI_STATUS = b"\xed\xf0\x31"
# Optional override: one hex byte in this file replaces 0x33 (for probing other icon values).
WIFI_FILE = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ecam.wifi")

# Names follow the ECAM enums in mmastrac/longshot (hardware_enums.rs); tags it lacks stay hex.
TAGS = {0x00: "temp", 0x01: "coffee_ml", 0x02: "strength", 0x03: "grind", 0x04: "blend",
        0x05: "infusion_speed", 0x06: "preinfusion", 0x07: "crema", 0x08: "2x", 0x09: "milk_0.1s",
        0x0A: "milk_temp", 0x0B: "milk_froth", 0x0C: "inversion", 0x0D: "water_temp", 0x0E: "tea_profile",
        0x0F: "water_ml", 0x19: "visible", 0x1A: "visible_in_prog", 0x1B: "index_length",
        0x1C: "accessory", 0x1F: "1f", 0x26: "26", 0x27: "27"}

STATE = {0x00: "standby", 0x01: "heating", 0x02: "turning off", 0x04: "descaling", 0x05: "steam",
         0x06: "recovery", 0x07: "ready", 0x08: "rinsing", 0x0A: "milk", 0x0B: "hot water",
         0x0C: "milk clean", 0x10: "chocolate"}
ACCESSORY = {0x00: "none", 0x01: "hot water spout", 0x02: "milk jug", 0x03: "chocolate",
             0x04: "milk jug (clean)", 0x06: "cold milk jug"}
# Monitor bytes [1..2] and [3..4] are little-endian u16 bit sets.
SWITCHES = [(1 << 0, ""), (1 << 1, "brew unit up"), (1 << 2, "brew unit down"), (1 << 3, "grounds container out"),
            (1 << 4, "tank out"), (1 << 5, "knob"), (1 << 6, "water low"), (1 << 7, "b7"),
            (1 << 8, "jug"), (1 << 9, "chocolate tank"), (1 << 10, "clean knob"),
            (1 << 13, "door open"), (1 << 14, "preground door open")]
ALARMS = [(1 << 0, "tank empty"), (1 << 1, "grounds full"), (1 << 2, "descale"), (1 << 3, "replace filter"),
          (1 << 4, "grind too fine"), (1 << 5, "beans empty"), (1 << 6, "service"), (1 << 7, "heater probe"),
          (1 << 8, "too much coffee"), (1 << 9, "brew unit motor"), (1 << 10, "steamer probe"),
          (1 << 11, "empty drip tray"), (1 << 12, "hydraulic problem"), (1 << 13, "insert tank"),
          (1 << 14, "clean knob"), (1 << 15, "beans empty")]
# Parameter 0x3F flag bits (cafy Parameter.ts); autostart is active when bit 0 is clear.
CONF_FLAGS = [(0x02, "12h clock"), (0x04, "beep"), (0x08, "cup light"), (0x10, "energy saving"), (0x80, "filter")]


def crc(b, c=0x1D0F):
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def frame(payload):
    body = bytes([0x0D, len(payload) + 3]) + payload
    return body + crc(body).to_bytes(2, "big")


# ---------------------------------------------------------------- daemon

class Bus:
    """Owns the tty. One request in flight; answers matched by command byte."""

    def __init__(self, dev):
        self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0                       # raw: no iflag/oflag/lflag processing
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN], a[6][termios.VTIME] = 1, 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        os.makedirs(LOGDIR, exist_ok=True)
        self.log = open(os.path.join(LOGDIR, time.strftime("uart_%Y%m%d_%H%M%S.bin")), "ab", buffering=0)
        self.lock = threading.Lock()
        self.answers = queue.Queue()
        self.status, self.status_at = None, 0.0
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        buf = b""
        while True:
            chunk = os.read(self.fd, 4096)
            self.log.write(chunk)
            buf += chunk
            while True:
                i = buf.find(b"\xd0")
                if i < 0:
                    buf = b""; break
                if len(buf) < i + 2:
                    buf = buf[i:]; break
                n = buf[i + 1]
                if len(buf) < i + n + 1:
                    buf = buf[i:]; break
                f = buf[i:i + n + 1]
                if n >= 4 and crc(f[:-2]) == int.from_bytes(f[-2:], "big"):
                    if f[2] == 0x75 and n == 0x12:
                        self.status, self.status_at = f[4:-2], time.time()
                    self.answers.put(f)
                    buf = buf[i + n + 1:]
                else:
                    buf = buf[i + 1:]

    def xfer(self, payload, timeout=0.8, retries=3, retry_if=None):
        """Send payload, return the D0 answer with the same command byte (or None)."""
        with self.lock:
            for attempt in range(retries):
                if attempt and retry_if and not retry_if():
                    return None
                while not self.answers.empty():
                    self.answers.get_nowait()
                os.write(self.fd, frame(payload))
                end = time.time() + timeout
                while (left := end - time.time()) > 0:
                    try:
                        f = self.answers.get(timeout=left)
                    except queue.Empty:
                        break
                    if f[2] == payload[0]:
                        return f
            return None

    def poll(self):
        wifi_at = 0
        while True:
            # The ESP repeats its wifi status every 5 s.
            if time.time() - wifi_at > 5:
                self.xfer(wifi_status(), retries=1, timeout=0.3)
                wifi_at = time.time()
            if time.time() - self.status_at > 0.9:
                self.xfer(b"\x75\x0f", retries=1)
            time.sleep(1)


def wifi_status():
    try:
        return b"\xed\xf0" + bytes([int(open(WIFI_FILE).read().strip(), 16)])
    except (OSError, ValueError):
        return WIFI_STATUS


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        bus = self.server.bus
        for line in self.rfile:
            req = json.loads(line)
            if req.get("op") == "status":
                st = bus.status
                out = {"status": st.hex() if st else None, "age": time.time() - bus.status_at}
            else:
                payload = bytes.fromhex(req["hex"])
                retry_if = None
                if req.get("only_if_idle"):
                    retry_if = lambda: bus.status is not None and bus.status[5] in (0x00, 0x07) and bus.status[6] == 0
                f = bus.xfer(payload, timeout=req.get("timeout", 0.8), retries=req.get("retries", 3), retry_if=retry_if)
                out = {"rx": f.hex() if f else None}
            self.wfile.write((json.dumps(out) + "\n").encode())


class Server(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True


def serve():
    bus = Bus(DEV)
    threading.Thread(target=bus.poll, daemon=True).start()
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    srv = Server(SOCK, Handler)
    srv.bus = bus
    os.chmod(SOCK, 0o600)
    print(f"ecamd: {DEV} -> {bus.log.name}, socket {SOCK}", flush=True)
    srv.serve_forever()


# ---------------------------------------------------------------- client

class Client:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX)
        try:
            self.s.connect(SOCK)
        except OSError:
            sys.exit(f"ecamd is not running ({SOCK}); start it: systemctl --user start ecamd")
        self.f = self.s.makefile("rw")

    def call(self, **req):
        self.f.write(json.dumps(req) + "\n"); self.f.flush()
        return json.loads(self.f.readline())

    def tx(self, payload, **kw):
        rx = self.call(op="tx", hex=payload.hex(), **kw)["rx"]
        return bytes.fromhex(rx) if rx else None

    def status(self):
        st = self.call(op="status")["status"]
        return bytes.fromhex(st) if st else None


def bits(v, table):
    names = [n for m, n in table if v & m and n]
    rest = v & ~sum(m for m, _ in table)
    return names + ([f"0x{rest:02x}"] if rest else [])


def describe(st):
    acc, state, phase, prog = st[0], st[5], st[6], st[7]
    switches, alarms = st[1] | st[2] << 8, st[3] | st[4] << 8
    # coffee runs inside state 07 with a non-zero phase (0x10 = finishing after stop)
    parts = ["brewing" if state == 0x07 and phase else STATE.get(state, f"state 0x{state:02x}")]
    if state not in (0x00, 0x07) or phase:
        parts.append(f"phase 0x{phase:02x} {prog}%")
    parts.append("accessory: " + ACCESSORY.get(acc, f"0x{acc:02x}"))
    if alarms:
        parts.append("ALARM " + ", ".join(bits(alarms, ALARMS)))
    sw = bits(switches, SWITCHES)
    if sw:
        parts.append("sw: " + ", ".join(sw))
    return " | ".join(parts) + f"   [{st.hex(' ')}]"


def drink_id(s):
    if s.lower() in DRINKS:
        return DRINKS[s.lower()]
    try:
        return int(s, 16)
    except ValueError:
        sys.exit(f"unknown drink {s!r}; see `ecam.py drinks` or pass a hex id")


def tlv(b):
    out, i = [], 0
    while i < len(b):
        t = b[i]; w = 2 if t in TAG16 else 1
        out.append((t, int.from_bytes(b[i + 1:i + 1 + w], "big"))); i += 1 + w
    return out


def utf16(name):
    raw = name.encode("utf-16-be")
    if len(raw) > 20:
        sys.exit("name too long (max 10 chars)")
    return raw.ljust(20, b"\0")


def name_of(raw):
    return raw.decode("utf-16-be").rstrip("\0")


def active_profile(c):
    a = c.tx(b"\xa9\xf0")
    return a[-3] if a else 1


def wait_done(c, timeout=300):
    seen, t0 = None, time.time()
    while time.time() - t0 < timeout:
        st = c.status()
        if st and st != seen:
            seen = st; print(time.strftime("%T"), describe(st), flush=True)
            if time.time() - t0 > 3 and st[5] == 0x07 and st[6] == 0:
                return
        time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("serve")
    p = sub.add_parser("status"); p.add_argument("-w", "--watch", action="store_true")
    sub.add_parser("on"); sub.add_parser("off"); sub.add_parser("drinks")
    p = sub.add_parser("brew"); p.add_argument("drink")
    p.add_argument("--coffee", type=int); p.add_argument("--water", type=int)
    p.add_argument("--milk", type=float, help="seconds"); p.add_argument("--strength", type=int)
    p.add_argument("--temp", choices=["low", "medium", "high", "max"], help="water temperature (tea)")
    p.add_argument("--wait", action="store_true")
    p = sub.add_parser("stop"); p.add_argument("drink", nargs="?")
    p = sub.add_parser("recipe"); p.add_argument("drink"); p.add_argument("-p", "--profile", type=int)
    sub.add_parser("profiles"); sub.add_parser("customs"); sub.add_parser("settings"); sub.add_parser("stats")
    p = sub.add_parser("delete-custom"); p.add_argument("slot", type=int, choices=range(1, 7))
    p = sub.add_parser("favorites"); p.add_argument("-p", "--profile", type=int)
    p.add_argument("action", nargs="?", choices=["set"]); p.add_argument("drinks", nargs="*")
    p = sub.add_parser("rename"); p.add_argument("slot", type=int); p.add_argument("name")
    p = sub.add_parser("set"); p.add_argument("what", choices=["temp", "hardness", "beep", "eco"]); p.add_argument("value")
    p = sub.add_parser("param"); p.add_argument("pid"); p.add_argument("value", nargs="?")
    p = sub.add_parser("raw"); p.add_argument("hex", nargs="+")
    a = ap.parse_args()

    if a.cmd == "serve":
        return serve()
    if a.cmd == "drinks":
        for k, v in sorted(DRINKS.items(), key=lambda kv: kv[1]):
            print(f"0x{v:02x}  {k}")
        return
    c = Client()

    def ack(f, ok=lambda f: True):
        if f is None:
            sys.exit("no answer")
        good = ok(f)
        print(("OK " if good else "REJECTED ") + f.hex(" "))
        if not good:
            sys.exit(1)

    if a.cmd == "status":
        if a.watch:
            return wait_done(c, timeout=float("inf"))
        st = c.status()
        print(describe(st) if st else "no monitor data yet")
    elif a.cmd == "on":
        ack(c.tx(b"\x84\x0f\x02\x01"))
    elif a.cmd == "off":
        ack(c.tx(b"\x84\x0f\x01\x01"))
    elif a.cmd == "brew":
        bev = drink_id(a.drink)
        params = b""
        if a.coffee is not None: params += b"\x01" + a.coffee.to_bytes(2, "big")
        if a.strength is not None: params += bytes([0x02, a.strength])
        if a.water is not None: params += b"\x0f" + a.water.to_bytes(2, "big")
        if a.milk is not None: params += b"\x09" + round(a.milk * 10).to_bytes(2, "big")
        # 0d = water temp 0..3 (tea; high=02 inferred). The app also sends 1b 01 with max, 1b 04 otherwise.
        if a.temp is not None:
            params += bytes([0x1B, 0x01 if a.temp == "max" else 0x04,
                             0x0D, ["low", "medium", "high", "max"].index(a.temp)])
        # Missing params come from the stored recipe; 83 answer f0 01 00 = accepted, 01 01 = rejected.
        f = c.tx(bytes([0x83, 0xF0, bev, 0x03]) + params + b"\x06", only_if_idle=True)
        ack(f, lambda f: f[4:6] == b"\x01\x00")
        with open(os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ecam.last"), "w") as fh:
            fh.write(f"{bev:02x}")
        if a.wait:
            wait_done(c)
    elif a.cmd == "stop":
        if a.drink:
            bev = drink_id(a.drink)
        else:
            try:
                bev = int(open(os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "ecam.last")).read(), 16)
            except OSError:
                sys.exit("no drink started from this CLI; pass the drink: ecam.py stop <drink>")
        ack(c.tx(bytes([0x83, 0xF0, bev, 0x02, 0x06])))
    elif a.cmd == "recipe":
        bev = drink_id(a.drink)
        prof = a.profile or active_profile(c)
        f = c.tx(bytes([0xA6, 0xF0, prof, bev]))
        if not f:
            sys.exit("no answer")
        print(f"profile {prof} drink 0x{bev:02x} {NAMES.get(bev, '')}")
        for t, v in tlv(f[6:-2]):
            print(f"  {TAGS.get(t, f'{t:02x}'):14} {v}" + (f"  ({v / 10:.1f} s)" if t == 0x09 else ""))
    elif a.cmd == "profiles":
        act = active_profile(c)
        f = c.tx(b"\xa4\xf0\x01\x04")
        body = f[6:-2] if f else b""
        for i in range(0, len(body), 22):
            slot = i // 22 + 1
            print(f"{'*' if slot == act else ' '} {slot}  {name_of(body[i:i + 20]):12} {body[i + 20:i + 22].hex(' ')}")
    elif a.cmd == "rename":
        cur = c.tx(bytes([0xA4, 0xF0, a.slot, 0x01]))
        if not cur:
            sys.exit("cannot read profile")
        # write tail observed from the app for an existing profile: 01 <mug>
        ack(c.tx(bytes([0xA5, 0xF0, a.slot, a.slot]) + utf16(a.name) + bytes([0x01, cur[-3]])),
            lambda f: f[4] == 0)
    elif a.cmd == "customs":
        f = c.tx(b"\xaa\xf0\x01\x06")
        body = f[6:-2] if f else b""
        for i in range(0, len(body), 21):
            slot = i // 21 + 1
            r = c.tx(bytes([0xA6, 0xF0, active_profile(c), 0xE5 + slot]))
            vis = dict(tlv(r[6:-2])).get(0x19) if r else None
            print(f"  {slot}  0x{0xE5 + slot:02x}  {name_of(body[i:i + 20]):12} img {body[i + 20]:02x}  "
                  f"{'in menu' if vis else 'hidden'}")
    elif a.cmd == "delete-custom":
        bev = 0xE5 + a.slot
        r = c.tx(bytes([0xA6, 0xF0, active_profile(c), bev]))
        if not r:
            sys.exit("cannot read recipe")
        # The app deletes by re-sending the stored recipe with 0b ff, 27 00 and tail 04 (save is 05).
        params = b"".join(bytes([t]) + v.to_bytes(2 if t in TAG16 else 1, "big")
                          for t, v in tlv(r[6:-2]) if t not in (0x0B, 0x19, 0x27))
        # Save/delete answer 00 01 (a started brew answers 01 00).
        ack(c.tx(bytes([0x83, 0xF0, bev, 0x00]) + params + b"\x0b\xff\x27\x00\x04"),
            lambda f: f[4:6] == b"\x00\x01")
    elif a.cmd == "favorites":
        prof = a.profile or active_profile(c)
        if a.action == "set":
            if len(a.drinks) > 11:
                sys.exit("max 11 favourites")
            # The app always writes the whole 11-entry list, 00 = empty.
            ids = bytes(drink_id(d) for d in a.drinks).ljust(11, b"\0")
            ack(c.tx(bytes([0xAD, 0xF0, prof]) + ids))
        else:
            # Answer: d0 12 ac f0 <profile> <11 drink ids, newest first, 00 = empty> crc16
            f = c.tx(bytes([0xAC, 0xF0, prof]))
            if not f:
                sys.exit("no answer")
            ids = [b for b in f[5:16] if b]
            print(f"profile {prof}: " + (", ".join(NAMES.get(b, f"0x{b:02x}") for b in ids) or "(none)"))
    elif a.cmd in ("settings", "set", "param"):
        def rd(pid):
            f = c.tx(b"\x95\x0f" + pid.to_bytes(2, "big") + b"\x01")
            return int.from_bytes(f[6:10], "big") if f else None

        def wr(pid, val):
            ack(c.tx(b"\x90\x0f" + pid.to_bytes(2, "big") + val.to_bytes(4, "big")), lambda f: f[6] == 0)

        if a.cmd == "settings":
            temp, hard, fl = rd(0x3D), rd(0x32), rd(0x3F)
            print(f"coffee temp   {['low', 'medium', 'high'][temp] if temp in (0, 1, 2) else temp}")
            print(f"hardness      level {hard + 1 if hard is not None else '?'}")
            for mask, name in CONF_FLAGS:
                print(f"{name:13} {'on' if fl & mask else 'off'}")
            h, m = rd(0x40), rd(0x41)
            print(f"autostart     {'off' if fl & 0x01 else 'on'} at {h:02d}:{m:02d}   (0x3F = 0x{fl:02x})")
            print(f"auto-off      {rd(0x3E)}")
        elif a.cmd == "set":
            if a.what == "temp":
                levels = {"low": 0, "medium": 1, "high": 2}
                if a.value not in levels:
                    sys.exit("temp: low | medium | high")
                wr(0x3D, levels[a.value])
            elif a.what == "hardness":
                wr(0x32, int(a.value) - 1)
            else:
                mask = 0x04 if a.what == "beep" else 0x10
                cur = rd(0x3F)
                if cur is None:
                    sys.exit("read failed, nothing written")
                wr(0x3F, cur | mask if a.value in ("on", "1") else cur & ~mask)
        else:
            pid = int(a.pid, 16)
            if a.value is None:
                v = rd(pid); print(f"0x{pid:04x} = {v} (0x{v:08x})" if v is not None else "no answer")
            else:
                wr(pid, int(a.value, 0))
    elif a.cmd == "stats":
        start, seen = 0, {}
        while True:
            f = c.tx(b"\xa2\x0f" + start.to_bytes(2, "big") + b"\x0a")
            if not f:
                break
            body, last = f[4:-2], None
            for i in range(0, len(body) - 5, 6):
                sid = int.from_bytes(body[i:i + 2], "big"); seen[sid] = int.from_bytes(body[i + 2:i + 6], "big"); last = sid
            if last is None or last < start:
                break
            start = last + 1
        for k in sorted(seen):
            print(k, seen[k])
    elif a.cmd == "raw":
        for h in a.hex:
            f = c.tx(bytes.fromhex(h), retries=1)
            print("TX", frame(bytes.fromhex(h)).hex(" ")); print("RX", f.hex(" ") if f else "-")


if __name__ == "__main__":
    main()
