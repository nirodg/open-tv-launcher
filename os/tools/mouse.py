#!/usr/bin/env python3
"""QMP pointer driver — real absolute mouse events into the guest.

  mouse.py <qmp.sock> move  <x> <y>     hover (moves focus)
  mouse.py <qmp.sock> click <x> <y>     left click (activates)
  mouse.py <qmp.sock> rclick <x> <y>    right click (item options)
  mouse.py <qmp.sock> key   <qemu-key>  key event (same names as sendkey)

HMP mouse_move is unreliable headless (-display none); QMP input-send-event
delivers real ABS/button events to the virtio-tablet, exactly like a GUI
window does.
"""
import json, socket, sys, time


class Qmp:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.settimeout(5)
        self.s.connect(path)
        self.f = self.s.makefile("rw")
        self._read()  # banner
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            line = self.f.readline()
            if not line:
                return None
            msg = json.loads(line)
            if "event" in msg:
                continue
            return msg

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        time.sleep(0.15)
        r = self._read()
        while r is None or "return" not in r and "error" not in r:
            r = self._read()
        return r


SCREEN_W, SCREEN_H = 1920, 1080  # guest design size
ABS_MAX = 32767                  # QEMU tablet axis range


def to_abs(px, screen):
    return max(0, min(ABS_MAX, round(px * ABS_MAX / (screen - 1))))


def send_abs(q, x, y):
    q.cmd("input-send-event", events=[
        {"type": "abs", "data": {"axis": "x", "value": to_abs(x, SCREEN_W)}},
        {"type": "abs", "data": {"axis": "y", "value": to_abs(y, SCREEN_H)}},
    ])


def send_btn(q, down, button="left"):
    q.cmd("input-send-event", events=[
        {"type": "btn", "data": {"down": down, "button": button}},
    ])


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    q = Qmp(sys.argv[1])
    what = sys.argv[2]
    if what == "move" and len(sys.argv) == 5:
        send_abs(q, int(sys.argv[3]), int(sys.argv[4]))
    elif what in ("click", "rclick") and len(sys.argv) == 5:
        x, y = int(sys.argv[3]), int(sys.argv[4])
        send_abs(q, x, y)
        time.sleep(0.1)
        send_btn(q, True, "right" if what == "rclick" else "left")
        time.sleep(0.1)
        send_btn(q, False, "right" if what == "rclick" else "left")
    elif what == "key" and len(sys.argv) == 4:
        q.cmd("input-send-event", events=[
            {"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": sys.argv[3]}}},
        ])
        time.sleep(0.05)
        q.cmd("input-send-event", events=[
            {"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": sys.argv[3]}}},
        ])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
