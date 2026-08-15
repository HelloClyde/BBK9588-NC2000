from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import socket
import struct
import threading
import time
from urllib.parse import urlparse
from urllib.request import Request, urlopen
import wave


AUDIO_HEADER = struct.Struct("<8sIIHHI")
AUDIO_MAGIC = b"BBKAUD1\0"


def read_exact(stream: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = stream.recv(size - len(chunks))
        if not chunk:
            raise EOFError("websocket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def read_ws_frame(stream: socket.socket) -> tuple[int, bytes]:
    first, second = read_exact(stream, 2)
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", read_exact(stream, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(stream, 8))[0]
    mask = read_exact(stream, 4) if masked else b""
    payload = bytearray(read_exact(stream, length))
    if mask:
        for index in range(length):
            payload[index] ^= mask[index & 3]
    return opcode, bytes(payload)


def connect_audio_ws(base_url: str) -> tuple[socket.socket, str, int]:
    parsed = urlparse(base_url)
    if parsed.scheme != "http" or not parsed.hostname:
        raise ValueError("only an http:// emulator URL is supported")
    host = parsed.hostname
    port = parsed.port or 80
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    stream = socket.create_connection((host, port), timeout=10)
    request = (
        f"GET /audio HTTP/1.1\r\nHost: {host}:{port}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    )
    stream.sendall(request.encode("ascii"))
    response = bytearray()
    while b"\r\n\r\n" not in response:
        response.extend(stream.recv(4096))
    if not response.startswith(b"HTTP/1.0 101") and not response.startswith(b"HTTP/1.1 101"):
        raise RuntimeError(response.decode("latin-1", errors="replace"))
    stream.settimeout(2.0)
    return stream, host, port


def post_touch(base_url: str, x: int, y: int, down: int) -> None:
    url = f"{base_url.rstrip('/')}/api/touch?x={x}&y={y}&down={down}"
    with urlopen(Request(url, method="POST"), timeout=10) as response:
        response.read()


def stats(samples: list[int]) -> dict[str, int | float]:
    if not samples:
        return {"samples": 0, "nonzero": 0, "peak": 0, "rms": 0.0}
    square_sum = sum(sample * sample for sample in samples)
    return {
        "samples": len(samples),
        "nonzero": sum(sample != 0 for sample in samples),
        "peak": max(abs(sample) for sample in samples),
        "rms": round(math.sqrt(square_sum / len(samples)), 2),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="抓取 BBK9588 模拟器 PCM，可选触发触摸键。")
    parser.add_argument("--url", default="http://127.0.0.1:8013")
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--trigger-after", type=float, default=1.5)
    parser.add_argument("--touch-x", type=int)
    parser.add_argument("--touch-y", type=int)
    parser.add_argument("--touch-hold", type=float, default=0.4)
    parser.add_argument("--output")
    args = parser.parse_args()
    if args.seconds <= 0 or args.trigger_after < 0:
        raise SystemExit("capture timing must be non-negative")
    if (args.touch_x is None) != (args.touch_y is None):
        raise SystemExit("touch-x and touch-y must be supplied together")

    stream, _, _ = connect_audio_ws(args.url)
    started = time.monotonic()
    trigger_time: list[float] = []
    packets: list[tuple[float, bytes]] = []

    def trigger() -> None:
        time.sleep(args.trigger_after)
        trigger_time.append(time.monotonic())
        post_touch(args.url, args.touch_x, args.touch_y, 1)
        time.sleep(args.touch_hold)
        post_touch(args.url, args.touch_x, args.touch_y, 0)

    worker = None
    if args.touch_x is not None:
        worker = threading.Thread(target=trigger, daemon=True)
        worker.start()

    sample_rate = 0
    channels = 0
    try:
        while time.monotonic() - started < args.seconds:
            try:
                opcode, payload = read_ws_frame(stream)
            except socket.timeout:
                continue
            if opcode == 8:
                break
            if opcode != 2 or len(payload) < AUDIO_HEADER.size:
                continue
            magic, _, rate, packet_channels, bits, payload_size = AUDIO_HEADER.unpack_from(payload)
            pcm = payload[AUDIO_HEADER.size:]
            if magic != AUDIO_MAGIC or bits != 16 or payload_size != len(pcm):
                continue
            if sample_rate and (rate != sample_rate or packet_channels != channels):
                raise RuntimeError("audio stream format changed during capture")
            sample_rate, channels = rate, packet_channels
            packets.append((time.monotonic(), pcm))
    finally:
        stream.close()
    if worker:
        worker.join(timeout=2)

    split = trigger_time[0] if trigger_time else float("inf")
    before_pcm = b"".join(pcm for received, pcm in packets if received < split)
    after_pcm = b"".join(pcm for received, pcm in packets if received >= split)
    all_pcm = before_pcm + after_pcm
    before = [value[0] for value in struct.iter_unpack("<h", before_pcm)]
    after = [value[0] for value in struct.iter_unpack("<h", after_pcm)]

    if args.output and all_pcm:
        with wave.open(args.output, "wb") as output:
            output.setnchannels(channels)
            output.setsampwidth(2)
            output.setframerate(sample_rate)
            output.writeframes(all_pcm)

    result = {
        "sample_rate": sample_rate,
        "channels": channels,
        "packets": len(packets),
        "before_trigger": stats(before),
        "after_trigger": stats(after),
        "pcm_sha256": hashlib.sha256(all_pcm).hexdigest(),
        "output": args.output,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if packets else 1


if __name__ == "__main__":
    raise SystemExit(main())
