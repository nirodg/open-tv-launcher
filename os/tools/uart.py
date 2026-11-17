#!/usr/bin/env python3
"""Check a CP2102 USB-UART adapter and the TV's debug serial port, at 115200 baud.

  uart.py                 same as `check`
  uart.py loopback        adapter self-test: short TX to RX with a jumper first
  uart.py check [SECS]    listen to the TV for SECS seconds (default 10), report what came in
  uart.py console         interactive console (Ctrl-] quits)

Wiring (3.3 V logic only, never the TV's 5 V or 12 V rails):
  CP2102 GND -> TV GND,  CP2102 RXD -> TV TX,  CP2102 TXD -> TV RX.
  Leave the CP2102 3V3/5V pin unconnected; the TV powers itself.

A check is most useful while the TV boots: start it, then power the TV on.
"""
import glob, os, select, sys, termios, time, tty

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing: pip install pyserial")

CP210X = (0x10C4, 0xEA60)
BAUD = 115200  # the TV's debug UART rate


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == CP210X:
            return p
    return None


def check_access(dev):
    if os.access(dev, os.R_OK | os.W_OK):
        return True
    print(f"  no read/write access to {dev}. Fix it once, then log out and back in:")
    print("    sudo usermod -aG dialout $USER")
    return False


def readable_score(data):
    if not data:
        return 0.0
    ok = sum(1 for b in data if 32 <= b < 127 or b in (9, 10, 13))
    return ok / len(data)


def loopback_once(dev, msg):
    with serial.Serial(dev, BAUD, timeout=0.5) as s:
        time.sleep(0.05)  # let the line settle so an open glitch isn't read as data
        s.reset_input_buffer()
        s.write(msg)
        return s.read(len(msg))


def loopback(dev):
    print(f"Loopback at {BAUD}: TXD must be jumpered to RXD on the adapter.")
    msg = b"open-tv-uart-loopback-0123456789\r\n"
    got = loopback_once(dev, msg)
    if got != msg:  # one retry before calling it a fail
        got = loopback_once(dev, msg)
    if got == msg:
        print("  OK: adapter works.")
        return True
    print(f"  FAIL, got {got!r}")
    print("  Check the jumper; if it is in place, the adapter is faulty.")
    return False


def check(dev, secs=10.0):
    print(f"Listening {secs:.0f} s at {BAUD}. Power-cycle the TV now for boot logs.")
    with serial.Serial(dev, BAUD, timeout=0.1) as s:
        s.reset_input_buffer()
        s.write(b"\r\n")  # prompts a waiting shell or bootloader to answer
        data, end = b"", time.time() + secs
        while time.time() < end:
            data += s.read(4096)
    score = readable_score(data)
    print(f"  {len(data)} bytes, {score:.0%} readable")
    if not data:
        print("\nNothing received. Check:")
        print("  - GND is shared, and RXD goes to the TV's TX (try swapping TX/RX)")
        print("  - the TV is on, or booting during the check")
        print("  - the pad really is the UART (TX idles at ~3.3 V on a multimeter)")
        return False
    if score < 0.8:
        print("\nData seen, but it doesn't read cleanly.")
        print("Likely a 1.8 V UART, a loose GND, or noise on a floating pin.")
        return False
    print("\nLink OK: the TV is talking. Last output:\n")
    print(data.decode("ascii", "replace")[-800:])
    return True


def console(dev):
    print(f"Console on {dev} at {BAUD}. Ctrl-] quits.")
    s = serial.Serial(dev, BAUD, timeout=0)
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        while True:
            r, _, _ = select.select([fd, s.fileno()], [], [])
            if s.fileno() in r:
                os.write(sys.stdout.fileno(), s.read(4096))
            if fd in r:
                ch = os.read(fd, 64)
                if b"\x1d" in ch:
                    break
                s.write(ch)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        s.close()
        print()


def main():
    p = find_port()
    if not p:
        print("CP2102 not found (USB 10c4:ea60). Plug it in and check `dmesg | tail`.")
        others = glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")
        if others:
            print("Other serial ports present:", ", ".join(others))
        sys.exit(1)
    print(f"CP2102 at {p.device} (serial {p.serial_number or '?'}, {p.location})")
    if not check_access(p.device):
        sys.exit(1)
    args = sys.argv[1:]
    cmd = args[0] if args else "check"
    if cmd == "loopback":
        sys.exit(0 if loopback(p.device) else 1)
    if cmd in ("check", "scan"):
        sys.exit(0 if check(p.device, float(args[1]) if len(args) > 1 else 10.0) else 1)
    if cmd == "console":
        console(p.device)
        return
    sys.exit(__doc__)


if __name__ == "__main__":
    main()
