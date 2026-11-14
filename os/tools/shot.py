#!/usr/bin/env python3
"""QEMU monitor helper (HMP over unix socket).

  shot.py <mon.sock> out.png                 # screenshot via screendump
  shot.py <mon.sock> --keys "down down" [--delay 0.4] [--shot out.png]

Keys use QEMU sendkey names: up down left right kp_enter esc backspace h m ...
"""
import socket, sys, time, os, subprocess, tempfile, argparse

def mon(sock_path, cmds, delay=0.4):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(5)
    s.connect(sock_path)
    time.sleep(0.2)
    try:
        s.recv(65536)  # banner
    except socket.timeout:
        pass
    out = []
    for c in cmds:
        s.sendall((c + "\n").encode())
        time.sleep(delay)
        try:
            out.append(s.recv(65536).decode(errors="replace"))
        except socket.timeout:
            out.append("")
    s.close()
    return "\n".join(out)

def screendump(sock_path, out_png, delay=0.6):
    with tempfile.TemporaryDirectory() as td:
        ppm = os.path.join(td, "s.ppm")
        r = mon(sock_path, [f"screendump {ppm}"], delay)
        if "Permission" in r or not os.path.exists(ppm):
            sys.exit(f"screendump failed: {r}")
        time.sleep(0.2)
        from PIL import Image
        Image.open(ppm).save(out_png)
        print(out_png)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sock")
    ap.add_argument("png", nargs="?")
    ap.add_argument("--keys", default="")
    ap.add_argument("--delay", type=float, default=0.4)
    ap.add_argument("--shot", dest="shot")
    a = ap.parse_args()
    if a.keys:
        ks = [k for k in a.keys.split() if k]
        mon(a.sock, [f"sendkey {k}" for k in ks], a.delay)
    if a.shot or a.png:
        screendump(a.sock, a.shot or a.png)

if __name__ == "__main__":
    main()
