"""
shrikevault_proto — host-side client for the ShrikeVault wire protocol.

Wire format (matches firmware/shrikevault/components/wallet_proto/):

    bytes  field        notes
      0-1  magic        b"SV"
        2  version      0x01
        3  msg_type
      4-5  payload_len  big-endian uint16
      6-N  payload
    N-3..  crc32        big-endian, zlib.crc32 (IEEE-802.3)

In the current build, requests are tunnelled over the USB-Serial-JTAG console
via the `wallet_req <hex>` command, and responses come back as a line matching
`<wallet_rsp:HEX>`. When we switch to TinyUSB-CDC later, only `_send_frame` /
`_recv_frame` change — the frame builders and message handlers stay identical.
"""
from __future__ import annotations

import re
import struct
import time
import zlib
from dataclasses import dataclass

try:
    import serial  # pyserial
    from serial.tools import list_ports
except ImportError as e:
    raise SystemExit("pyserial is required:  pip install pyserial") from e

# ---- wire-protocol constants (mirror wallet_proto.h) ------------------------

MAGIC = b"SV"
VERSION = 0x01
HEADER_BYTES = 6
TRAILER_BYTES = 4
OVERHEAD = HEADER_BYTES + TRAILER_BYTES
PAYLOAD_MAX = 4096
FRAME_MAX = PAYLOAD_MAX + OVERHEAD

MSG_DEVICE_INFO = 0x01
MSG_GET_ADDRESS = 0x02
MSG_SIGN_DIGEST = 0x03
MSG_SIGN_ETH    = 0x04
MSG_ERROR       = 0xFF

ERR_NAMES = {
    0x01: "BAD_MAGIC",     0x02: "BAD_VERSION",   0x03: "BAD_LENGTH",
    0x04: "BAD_CRC",       0x05: "UNKNOWN_MSG",   0x06: "BAD_PAYLOAD",
    0x07: "NO_DEVICE_KEY", 0xFE: "INTERNAL",
}

ESP32S3_USB_SERIAL_JTAG = (0x303A, 0x1001)


class WalletError(Exception):
    """Raised when the device returns an ERROR frame or framing fails."""


# ---- frame build / parse ----------------------------------------------------

def build_frame(msg_type: int, payload: bytes = b"") -> bytes:
    if len(payload) > PAYLOAD_MAX:
        raise ValueError(f"payload too big: {len(payload)} > {PAYLOAD_MAX}")
    hdr = MAGIC + bytes([VERSION, msg_type]) + struct.pack(">H", len(payload))
    body = hdr + payload
    crc = zlib.crc32(body) & 0xFFFFFFFF
    return body + struct.pack(">I", crc)


def parse_frame(buf: bytes) -> tuple[int, bytes]:
    """Returns (msg_type, payload). Raises WalletError on framing / CRC errors."""
    if len(buf) < OVERHEAD:
        raise WalletError(f"frame too short: {len(buf)} < {OVERHEAD}")
    if buf[:2] != MAGIC:
        raise WalletError(f"bad magic: {buf[:2]!r}")
    if buf[2] != VERSION:
        raise WalletError(f"bad version: 0x{buf[2]:02x}")
    msg_type = buf[3]
    plen = struct.unpack(">H", buf[4:6])[0]
    if len(buf) != OVERHEAD + plen:
        raise WalletError(f"length mismatch: frame={len(buf)} but header says payload={plen}")
    payload = buf[HEADER_BYTES:HEADER_BYTES + plen]
    crc_got = struct.unpack(">I", buf[HEADER_BYTES + plen:])[0]
    crc_want = zlib.crc32(buf[:HEADER_BYTES + plen]) & 0xFFFFFFFF
    if crc_got != crc_want:
        raise WalletError(f"bad CRC: got 0x{crc_got:08x} want 0x{crc_want:08x}")
    if msg_type == MSG_ERROR:
        code = payload[0] if payload else 0
        reason = payload[1:].split(b"\0", 1)[0].decode("utf-8", "replace")
        raise WalletError(f"device error: {ERR_NAMES.get(code, f'0x{code:02x}')} — {reason}")
    return msg_type, payload


# ---- decoded results --------------------------------------------------------

@dataclass
class DeviceInfo:
    banner: str
    fw_major: int
    fw_minor: int
    provisioned: bool
    chip_target: int
    address_fingerprint: str | None  # short like "0x3B94...A02d"


def parse_device_info(payload: bytes) -> DeviceInfo:
    # banner: NUL-terminated string at the start
    nul = payload.index(b"\0")
    banner = payload[:nul].decode("ascii")
    o = nul + 1
    fw_major, fw_minor, prov, chip = payload[o:o + 4]
    o += 4
    fp = None
    if prov and o < len(payload):
        fp = payload[o:].split(b"\0", 1)[0].decode("ascii", "replace")
    return DeviceInfo(banner, fw_major, fw_minor, bool(prov), chip, fp)


@dataclass
class EthSignature:
    r: bytes  # 32 B big-endian
    s: bytes  # 32 B big-endian
    recovery_id: int  # 0 or 1

    @property
    def raw(self) -> bytes:
        return self.r + self.s + bytes([self.recovery_id])

    def v_legacy_eip155(self, chain_id: int) -> int:
        return chain_id * 2 + 35 + self.recovery_id

    def v_eip1559(self) -> int:
        return self.recovery_id


def parse_signature(payload: bytes) -> EthSignature:
    if len(payload) != 65:
        raise WalletError(f"signature payload must be 65 bytes, got {len(payload)}")
    return EthSignature(r=payload[:32], s=payload[32:64], recovery_id=payload[64])


# ---- client -----------------------------------------------------------------

class WalletClient:
    """
    Opens the ShrikeVault USB serial console, exchanges wire-protocol frames.

    Current transport: `wallet_req <hex>` console command + `<wallet_rsp:HEX>`
    response line. When the firmware switches to TinyUSB-CDC raw framing, only
    _exchange() needs to change.
    """
    _RSP_RE = re.compile(rb"<wallet_rsp:([0-9a-fA-F]+)>")

    def __init__(self, port: str, baud: int = 115200, timeout: float = 5.0):
        self._port = port
        self._timeout = timeout
        # CRITICAL: do NOT toggle DTR/RTS on open — on the ESP32-S3
        # USB-Serial-JTAG the default pulse triggers a hardware reset, which
        # makes every fresh connection race the boot. Set both lines low
        # BEFORE opening the port.
        s = serial.Serial()
        s.port = port
        s.baudrate = baud
        s.timeout = 0.3
        s.write_timeout = 2.0
        s.dtr = False
        s.rts = False
        s.open()
        self._s = s
        self._s.reset_input_buffer()
        # Nudge the REPL so we have a clean prompt before our first request.
        time.sleep(0.1)
        self._drain(0.1)

    def __enter__(self): return self
    def __exit__(self, *a): self.close()
    def close(self):
        try: self._s.close()
        except Exception: pass

    def _drain(self, secs: float) -> bytes:
        end = time.time() + secs
        buf = b""
        while time.time() < end:
            chunk = self._s.read(8192)
            if chunk: buf += chunk
        return buf

    @staticmethod
    def find_port() -> str | None:
        """Find the ESP32-S3 USB-Serial-JTAG (Shrike-Fi) by VID/PID."""
        for p in list_ports.comports():
            if p.vid and p.pid and (p.vid, p.pid) == ESP32S3_USB_SERIAL_JTAG:
                return p.device
        return None

    # -- low-level exchange (console-tunnel; replace for TinyUSB-CDC) -------

    def _exchange(self, req: bytes) -> bytes:
        cmd = b"wallet_req " + req.hex().encode("ascii") + b"\r\n"
        self._s.write(cmd)
        self._s.flush()
        # Read until we see <wallet_rsp:...> or timeout.
        end = time.time() + self._timeout
        buf = b""
        while time.time() < end:
            chunk = self._s.read(16384)
            if chunk: buf += chunk
            m = self._RSP_RE.search(buf)
            if m: return bytes.fromhex(m.group(1).decode("ascii"))
        raise WalletError(f"timeout waiting for response (got {len(buf)} bytes; tail: {buf[-200:]!r})")

    # -- high-level message API --------------------------------------------

    def device_info(self) -> DeviceInfo:
        msg_type, p = parse_frame(self._exchange(build_frame(MSG_DEVICE_INFO)))
        assert msg_type == MSG_DEVICE_INFO
        return parse_device_info(p)

    def get_address(self, index: int) -> str:
        req = build_frame(MSG_GET_ADDRESS, struct.pack(">I", index))
        msg_type, p = parse_frame(self._exchange(req))
        assert msg_type == MSG_GET_ADDRESS
        return p.split(b"\0", 1)[0].decode("ascii")

    def sign_digest(self, index: int, digest: bytes) -> EthSignature:
        if len(digest) != 32:
            raise ValueError("digest must be 32 bytes")
        req = build_frame(MSG_SIGN_DIGEST, struct.pack(">I", index) + digest)
        msg_type, p = parse_frame(self._exchange(req))
        assert msg_type == MSG_SIGN_DIGEST
        return parse_signature(p)

    def sign_eth(self, index: int, msg: bytes) -> EthSignature:
        req = build_frame(MSG_SIGN_ETH, struct.pack(">I", index) + msg)
        msg_type, p = parse_frame(self._exchange(req))
        assert msg_type == MSG_SIGN_ETH
        return parse_signature(p)
