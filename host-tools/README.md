# `host-tools/` — PC-side helpers

| File | Status | What |
|---|---|---|
| [`probe_board.py`](probe_board.py) | ✅ | list serial ports, flag the Shrike-Fi's ESP32-S3 USB Serial/JTAG, open a port and run a couple of `bringup` console commands. Dependency: `pyserial`. |
| `puf_analyze.py` | ⚪ planned (P1) | ingest many PUF challenge/response dumps across power cycles (and temperature if you can) → uniqueness / reliability / min-entropy estimates → picks ECC parameters for the fuzzy extractor |
| `shrikevault_cli.py` | ⚪ planned (P4) | the actual wallet client: speaks the USB-CDC framed protocol — `GET_ADDRESS`, `SIGN_ETH_TX`, `SIGN_MESSAGE`, … — plus a `web3.py` external-signer shim and an end-to-end "build → confirm on device → broadcast" example |

## Setup

```bash
pip install pyserial            # for probe_board.py
# later: pip install web3 eth-account cbor2   (for the wallet client)
```

## Quick start

```bash
python host-tools/probe_board.py                  # which port is the board on?
python host-tools/probe_board.py --port COM19     # read its banner, run "info"
python host-tools/probe_board.py --port COM19 --cmd info --cmd fpga_load --cmd io_read
```
