#!/usr/bin/env python3
"""
Groovy_MiSTer mock endpoint — a software stand-in for the FPGA side.

This is NOT an emulator. It only answers the handshake/ACK packets so that
`gmw_init()` succeeds and the RPCS3 send pipeline (capture -> downscale ->
readback -> BGR pack -> UDP send -> sender pacing -> heartbeat) can be
smoke-tested on a single machine with no MiSTer hardware.

It does NOT decode or display pixels. "It works" here means: RPCS3 connects,
blits flow, and the GROOVY heartbeat shows echo advancing + vramSynced=1.

Run this BEFORE launching RPCS3, with config:
    GroovyMister:
      Enabled: true
      MiSTer Host: 127.0.0.1

Usage:  python3 groovy_mock_endpoint.py [--port 32100] [--verbose]
Stop:   Ctrl+C
"""

import argparse
import socket
import struct
import sys
import time

# Opcodes (groovymister.cpp)
CMD_CLOSE            = 1
CMD_INIT             = 2
CMD_SWITCHRES        = 3
CMD_AUDIO            = 4
CMD_GET_STATUS       = 5
CMD_BLIT_VSYNC       = 6
CMD_BLIT_FIELD_VSYNC = 7
CMD_GET_VERSION      = 8

NAMES = {
    CMD_CLOSE: "CLOSE", CMD_INIT: "INIT", CMD_SWITCHRES: "SWITCHRES",
    CMD_AUDIO: "AUDIO", CMD_GET_STATUS: "GET_STATUS", CMD_BLIT_VSYNC: "BLIT",
    CMD_BLIT_FIELD_VSYNC: "BLIT_FIELD", CMD_GET_VERSION: "GET_VERSION",
}

# 13-byte ACK layout (see setFpgaStatus):
#   [0:4]  frameEcho   (u32 LE)
#   [4:6]  vCountEcho  (u16 LE)
#   [6:10] frame       (u32 LE)
#   [10:12] vCount     (u16 LE)
#   [12]   status bits: bit0 vramReady, bit1 vramEndFrame, bit2 vramSynced,
#                       bit3 vgaFrameskip, bit4 vgaVblank, bit5 vgaF1,
#                       bit6 audio, bit7 vramQueue
STATUS_VRAM_READY  = 0x01
STATUS_VRAM_SYNCED = 0x04   # "real picture, not red screen"


def make_ack(frame_echo: int) -> bytes:
    bits = STATUS_VRAM_READY | STATUS_VRAM_SYNCED
    return struct.pack("<IHIHB",
                       frame_echo & 0xFFFFFFFF,  # frameEcho
                       0,                        # vCountEcho
                       frame_echo & 0xFFFFFFFF,  # frame
                       0,                        # vCount
                       bits)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=32100)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    sock.bind((args.bind, args.port))

    print(f"[mock] Listening on {args.bind}:{args.port} (UDP). Ctrl+C to stop.")
    print("[mock] This only ACKs the protocol; it does not display pixels.")

    echo = 0
    blits = 0
    bytes_in = 0
    last_report = time.time()

    while True:
        data, addr = sock.recvfrom(65535)
        if not data:
            continue
        op = data[0]
        bytes_in += len(data)

        if op == CMD_INIT:
            echo += 1
            sock.sendto(make_ack(echo), addr)
            print(f"[mock] INIT from {addr} -> ACK (echo={echo})")
        elif op == CMD_GET_VERSION:
            # 1-byte reply = core version. Keep <2 so the predictor stays off.
            sock.sendto(bytes([1]), addr)
            if args.verbose:
                print("[mock] GET_VERSION -> v1")
        elif op in (CMD_BLIT_VSYNC, CMD_BLIT_FIELD_VSYNC):
            echo += 1
            blits += 1
            sock.sendto(make_ack(echo), addr)
            if args.verbose:
                print(f"[mock] BLIT #{blits} ({len(data)}B) -> ACK (echo={echo})")
        elif op in (CMD_GET_STATUS, CMD_AUDIO):
            echo += 1
            sock.sendto(make_ack(echo), addr)
        elif op == CMD_SWITCHRES:
            print(f"[mock] SWITCHRES received ({len(data)}B) — modeline accepted")
        elif op == CMD_CLOSE:
            print("[mock] CLOSE received")
        else:
            if args.verbose:
                print(f"[mock] unknown op {op} ({len(data)}B)")

        now = time.time()
        if now - last_report >= 2.0:
            mbps = (bytes_in * 8) / (now - last_report) / 1e6
            print(f"[mock] {blits} blits total | ~{mbps:.1f} Mbit/s inbound | echo={echo}")
            bytes_in = 0
            last_report = now


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n[mock] stopped.")
