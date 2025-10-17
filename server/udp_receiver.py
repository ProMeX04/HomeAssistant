"""UDP audio receiver và pipeline Whisper cho ESP32 + INMP441.

Script lắng nghe UDP từ firmware trong ``src/main.cpp``. Mỗi gói có header 12 byte
(sequence, timestamp, số mẫu, sample rate) theo network-order, payload là PCM
16-bit little-endian. Khi nhận thấy một phiên ghi (công tắc trên ESP32 bật),
server gom audio, chuyển thành WAV và tự động chuyển giọng nói sang văn bản
bằng Whisper (ưu tiên tiếng Việt).

Ví dụ chạy::

    python server/udp_receiver.py --host 0.0.0.0 --port 5000 \
        --session-dir recordings --whisper-model small --language vi

Script sẽ ghi log thống kê gói, tạo file WAV cho từng phiên (nếu chỉ định) và
in ra văn bản đã nhận dạng khi kết thúc phiên (khi công tắc tắt hoặc không có dữ
liệu vượt quá timeout).
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Optional

try:  # Optional until Whisper được bật
    import numpy as np
except ImportError:  # pragma: no cover - handled at runtime
    np = None

try:
    import whisper
except ImportError:  # pragma: no cover - handled at runtime
    whisper = None

HEADER_STRUCT = struct.Struct("!IIHH")
HEADER_SIZE = HEADER_STRUCT.size
EXPECTED_SAMPLE_RATE = 16000


@dataclass
class PacketStats:
    """Track packet-level statistics for diagnostics."""

    first_timestamp_ms: Optional[int] = None
    last_timestamp_ms: Optional[int] = None
    last_sequence: Optional[int] = None
    lost_packets: int = 0
    received_packets: int = 0

    def update(self, sequence: int, timestamp_ms: int) -> None:
        if self.first_timestamp_ms is None:
            self.first_timestamp_ms = timestamp_ms
        if self.last_sequence is not None and ((self.last_sequence + 1) & 0xFFFFFFFF) != sequence:
            gap = (sequence - self.last_sequence) & 0xFFFFFFFF
            if gap:
                self.lost_packets += gap
        self.last_sequence = sequence
        self.last_timestamp_ms = timestamp_ms
        self.received_packets += 1

    def summary(self) -> str:
        if self.first_timestamp_ms is None or self.last_timestamp_ms is None:
            return "No packets received"
        duration_s = (self.last_timestamp_ms - self.first_timestamp_ms) / 1000.0
        rate = self.received_packets / duration_s if duration_s > 0 else float("inf")
        return (
            f"Packets: {self.received_packets}, lost: {self.lost_packets}, "
            f"duration: {duration_s:.2f}s, rate: {rate:.1f}/s"
        )


@dataclass
class SessionBuffer:
    """Hold audio payloads for một phiên ghi."""

    created_monotonic: float
    last_packet_monotonic: float
    sample_rate: int
    source: tuple[str, int]
    stats: PacketStats = field(default_factory=PacketStats)
    payloads: list[bytes] = field(default_factory=list)

    def add_packet(self, payload: bytes, sequence: int, timestamp_ms: int, now: float) -> None:
        self.payloads.append(payload)
        self.stats.update(sequence, timestamp_ms)
        self.last_packet_monotonic = now

    def merge_audio(self) -> bytes:
        return b"".join(self.payloads)


def parse_arguments(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Receive UDP audio from ESP32")
    parser.add_argument("--host", default="0.0.0.0", help="Local interface to bind (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=5000, help="UDP port to listen on (default: 5000)")
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Optional WAV file path to store toàn bộ luồng (ghi liên tục)",
    )
    parser.add_argument(
        "--flush",
        type=float,
        default=5.0,
        help="Flush interval in seconds for printing statistics (default: 5s)",
    )
    parser.add_argument(
        "--buffer",
        type=int,
        default=4096,
        help="Receive buffer size passed to recvfrom (default: 4096 bytes)",
    )
    parser.add_argument(
        "--session-timeout",
        type=float,
        default=1.0,
        help="Khoảng im lặng (s) để kết thúc một phiên ghi",
    )
    parser.add_argument(
        "--session-dir",
        type=Path,
        default=None,
        help="Thư mục lưu WAV cho từng phiên (tự tạo nếu chưa có)",
    )
    parser.add_argument(
        "--whisper-model",
        default="small",
        help="Tên model Whisper (tiny/small/medium/large-v3, ...)",
    )
    parser.add_argument(
        "--language",
        default="vi",
        help="Mã ngôn ngữ ISO cho Whisper (mặc định: vi)",
    )
    parser.add_argument(
        "--no-whisper",
        action="store_true",
        help="Tắt chuyển giọng nói sang văn bản (giữ hành vi ghi âm cũ)",
    )
    return parser.parse_args(argv)


def open_wave_file(path: Path, sample_rate: int) -> wave.Wave_write:
    wf = wave.open(str(path), "wb")
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(sample_rate)
    return wf


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = parse_arguments(argv)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.settimeout(1.0)

    if args.session_dir:
        args.session_dir.mkdir(parents=True, exist_ok=True)

    active_session: Optional[SessionBuffer] = None
    wav: Optional[wave.Wave_write] = None
    last_flush = time.monotonic()
    expected_sample_rate: Optional[int] = None
    whisper_model = None
    whisper_available = False

    if args.no_whisper:
        print("Whisper transcription disabled (--no-whisper)")
    else:
        if whisper is None or np is None:
            print(
                "Whisper hoặc numpy chưa được cài đặt. Cài đặt bằng 'pip install openai-whisper numpy torch'",
                file=sys.stderr,
            )
        else:
            try:
                # Trên Mac mini M4 nên chạy trên CPU (Metal/PyTorch sẽ tự chọn)
                whisper_model = whisper.load_model(args.whisper_model)
                whisper_available = True
                print(f"Loaded Whisper model '{args.whisper_model}'")
            except Exception as exc:  # pragma: no cover - runtime safety
                print(f"Không thể load Whisper model: {exc}", file=sys.stderr)

    if args.output:
        print(f"Sẽ ghi toàn bộ luồng vào {args.output}")

    print(f"Listening on {args.host}:{args.port} ...")
    try:
        while True:
            try:
                data, addr = sock.recvfrom(args.buffer)
            except socket.timeout:
                pass
            else:
                if len(data) < HEADER_SIZE:
                    print(f"Ignoring short packet ({len(data)} bytes) from {addr}")
                    continue
                sequence, timestamp_ms, sample_count, sample_rate = HEADER_STRUCT.unpack_from(data)
                payload = data[HEADER_SIZE:]
                expected_bytes = sample_count * 2
                if len(payload) != expected_bytes:
                    print(
                        f"Ignoring malformed packet seq={sequence} from {addr}: "
                        f"expected {expected_bytes} payload bytes, got {len(payload)}"
                    )
                    continue

                now = time.monotonic()

                if args.output and wav is None:
                    wav = open_wave_file(args.output, sample_rate)
                    expected_sample_rate = sample_rate
                    print(f"Đang ghi liên tục vào {args.output} @ {sample_rate} Hz")
                elif wav is not None and expected_sample_rate != sample_rate:
                    print(
                        f"Sample rate thay đổi từ {expected_sample_rate} → {sample_rate}, đóng file {args.output}",
                        file=sys.stderr,
                    )
                    wav.close()
                    wav = None
                    expected_sample_rate = None

                if active_session is None:
                    active_session = SessionBuffer(
                        created_monotonic=now,
                        last_packet_monotonic=now,
                        sample_rate=sample_rate,
                        source=addr,
                    )
                    print(
                        f"▶️  Bắt đầu phiên mới từ {addr[0]}:{addr[1]} (sample_rate={sample_rate})"
                    )
                elif sample_rate != active_session.sample_rate:
                    print(
                        "Sample rate thay đổi trong phiên, kết thúc phiên hiện tại trước khi tạo phiên mới."
                    )
                    finalize_session(
                        active_session,
                        whisper_available,
                        whisper_model,
                        args,
                    )
                    active_session = SessionBuffer(
                        created_monotonic=now,
                        last_packet_monotonic=now,
                        sample_rate=sample_rate,
                        source=addr,
                    )

                active_session.add_packet(payload, sequence, timestamp_ms, now)

                if wav is not None:
                    wav.writeframes(payload)

            now = time.monotonic()
            if now - last_flush >= args.flush:
                if active_session is not None:
                    print(active_session.stats.summary())
                else:
                    print("Chưa nhận phiên nào")
                last_flush = now

            if (
                active_session is not None
                and now - active_session.last_packet_monotonic >= args.session_timeout
            ):
                finalize_session(active_session, whisper_available, whisper_model, args)
                active_session = None
    except KeyboardInterrupt:
        print("\nStopping receiver...")
    finally:
        sock.close()
        if wav is not None:
            wav.close()
        if active_session is not None:
            finalize_session(active_session, whisper_available, whisper_model, args)
    return 0


def finalize_session(
    session: SessionBuffer,
    whisper_available: bool,
    whisper_model,
    args: argparse.Namespace,
) -> None:
    """Kết thúc một phiên: lưu WAV và chuyển giọng nói sang văn bản."""

    print(
        f"⏹️  Kết thúc phiên từ {session.source[0]}:{session.source[1]} — {session.stats.summary()}"
    )

    audio_bytes = session.merge_audio()
    if not audio_bytes:
        print("(Không có dữ liệu audio trong phiên)" )
        return

    if args.session_dir:
        timestamp = time.strftime("%Y%m%d-%H%M%S")
        base_name = f"session_{timestamp}_{session.source[0].replace('.', '-')}_{session.source[1]}"
        file_path = args.session_dir / f"{base_name}.wav"
        counter = 1
        while file_path.exists():
            file_path = args.session_dir / f"{base_name}_{counter}.wav"
            counter += 1
        wf = open_wave_file(file_path, session.sample_rate)
        try:
            wf.writeframes(audio_bytes)
        finally:
            wf.close()
        print(f"Đã lưu WAV phiên vào {file_path}")

    if not whisper_available:
        return

    if session.sample_rate != EXPECTED_SAMPLE_RATE:
        print(
            f"Whisper yêu cầu sample rate {EXPECTED_SAMPLE_RATE} Hz (nhận {session.sample_rate}). Bỏ qua phiên."
        )
        return

    assert np is not None and whisper is not None  # for type checkers
    audio = np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32768.0
    if audio.size == 0:
        print("Không có mẫu audio để nhận dạng")
        return

    duration = audio.size / EXPECTED_SAMPLE_RATE
    print(
        f"📝  Đang nhận dạng {duration:.2f}s audio bằng Whisper model '{args.whisper_model}' (ngôn ngữ={args.language})"
    )

    try:
        result = whisper_model.transcribe(audio, language=args.language, fp16=False)
        text = result.get("text", "").strip()
        if text:
            print(f"→ Văn bản: {text}")
        else:
            print("Whisper không tạo ra văn bản")
    except Exception as exc:  # pragma: no cover - runtime safety
        print(f"Lỗi khi chạy Whisper: {exc}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
