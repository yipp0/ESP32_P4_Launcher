#!/usr/bin/env python3
"""Interactive SDIO utility console. Saves and verifies a C6 backup before acknowledging it."""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import threading
import time
import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True)
    ap.add_argument('--backup', type=Path, required=True)
    args = ap.parse_args()
    if args.backup.exists() or args.backup.with_suffix('.partial').exists():
        raise SystemExit('Backup destination already exists; preserve it and choose a new path')
    s = serial.Serial()
    s.port = args.port
    s.baudrate = 921600
    s.timeout = .1
    s.dtr = s.rts = False
    s.open()
    ready, connected = threading.Event(), threading.Event()
    done = threading.Event()

    def reader():
        try:
            while not done.is_set():
                raw = s.readline()
                if not raw:
                    continue
                line = raw.decode(errors='replace').strip()
                if line.startswith('BACKUP DATA '):
                    _, _, size, expected = line.split()
                    size = int(size)
                    if size != 4194304 or len(expected) != 32:
                        raise ValueError('invalid backup header')
                    partial = args.backup.with_suffix('.partial')
                    digest = hashlib.md5()
                    fd = os.open(partial, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
                    with os.fdopen(fd, 'wb') as f:
                        left = size
                        deadline = time.monotonic() + 120
                        while left:
                            data = s.read(min(16384, left))
                            if not data:
                                if time.monotonic() > deadline:
                                    raise TimeoutError('backup transfer stalled')
                                continue
                            f.write(data)
                            digest.update(data)
                            left -= len(data)
                        f.flush()
                        os.fsync(f.fileno())
                    if digest.hexdigest() != expected:
                        raise ValueError('backup MD5 mismatch; partial file preserved')
                    # Verify what is on disk, as well as the received stream.
                    if hashlib.md5(partial.read_bytes()).hexdigest() != expected:
                        raise ValueError('saved backup MD5 mismatch')
                    partial.rename(args.backup)
                    print(f'HOST: saved and verified {size} bytes, MD5 {expected}', flush=True)
                    s.write(b'backup-ok\n')
                    continue
                print(line, flush=True)
                if 'C6 SDIO TOOL READY' in line:
                    ready.set()
                if line.startswith('CONNECT OK'):
                    connected.set()
        except Exception as e:
            print(f'HOST ERROR: {e}', file=sys.stderr, flush=True)
            done.set()

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        if not ready.wait(12):
            raise RuntimeError('utility did not become ready')
        for attempt in range(2):
            s.write(b'\nconnect\n')
            if connected.wait(15):
                break
        if connected.is_set():
            print('HOST: connected; requesting full backup', flush=True)
            s.write(b'backup\n')
        else:
            print('HOST: connect failed; retry with connect while holding C6 BOOT', flush=True)
        for line in sys.stdin:
            if done.is_set():
                break
            s.write(line.encode())
            s.flush()
    finally:
        done.set()
        thread.join(timeout=.5)
        s.close()


if __name__ == '__main__':
    main()
