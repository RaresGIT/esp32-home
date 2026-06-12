# Window Controller — ESP32-C3 433 MHz bridge

DIY controller for a 433 MHz remote-controlled window. Learns the remote's codes,
replays them on demand, hosts its own dashboard, and pairs **directly with Apple
Home** as a native HomeKit accessory (HomeSpan) — no hub, bridge, or server
required. Optional MQTT + Home Assistant discovery is also built in.

Hardware: **LOLIN (Wemos) C3 Mini v2.1** + 433 MHz transmitter/receiver pair.

## Apple Home

The device is a native HomeKit Window accessory. Pair it from the dashboard:
**Settings → Apple Home pairing** shows the QR code (manual code `471-02-386`) —
scan it with the iPhone Home app (+ → Add Accessory → Add Anyway at the
uncertified-accessory warning). HAP runs on port 1201; the dashboard stays on 80.
Control works locally from any iOS device on the same network; add a HomePod or
Apple TV as a home hub for remote access and automations.

The onboard RGB LED mirrors the window state: green = open, red = closed,
off = unknown. With no position feedback from the window, state is inferred
from the last command sent.

## Wiring

| Module | Pin | C3 Mini | Notes |
|---|---|---|---|
| Receiver | DATA | GPIO **1** | |
| Receiver | VCC | 3.3V | If capture is weak, try VBUS (5V) — most kit receivers tolerate both |
| Transmitter | DATA | GPIO **0** | |
| Transmitter | VCC | VBUS (5V) | More voltage = more range |
| Both | GND | GND | |

**Antennas are not optional.** Solder a straight 17.3 cm wire (quarter-wave for
433.92 MHz) to the ANT pad of *both* modules. Without them, range is centimeters.

Avoid GPIO 8/9 for anything — they are boot-strapping pins on the C3.

## Build & flash

Requires [PlatformIO](https://platformio.org/) (CLI or VS Code extension).

```sh
pio run -t upload      # build + flash firmware (USB)
pio run -t uploadfs    # flash the dashboard (data/ -> LittleFS)
pio device monitor     # serial log @ 115200
```

Both steps are needed on first flash — the dashboard lives in the LittleFS
partition, separate from the firmware.

## First boot

1. The device starts a Wi-Fi AP **`windowctl-XXXXXX`** (password `windowctl`).
2. Connect to it and open **http://192.168.4.1/** → Settings → Wi-Fi → scan,
   pick your network, save. The device reboots onto your network.
3. From then on the dashboard is at **http://windowctl.local/** (or the IP shown
   on the serial monitor / your router).

## Learning your remote

1. Open the **Sniffer** tab and press buttons on the 433 MHz remote.
2. Each press should show up decoded (code / protocol / bit length). Click
   **Replay** while pointing at the window to confirm the capture actually works.
3. **Save** working signals. Name them **`open`**, **`close`** and (if the remote
   has one) **`stop`** — those names map to the Home Assistant cover entity.
4. If presses show as *undecodable burst* or nothing at all, switch the receiver
   to **Raw** mode and capture the exact pulse train instead. Raw captures replay
   verbatim and work with almost any fixed-code protocol.

Tip: when you replay, the receiver hears your own transmitter — seeing the echo
appear in the sniffer is a quick confirmation that the transmitter is working.

## Home Assistant

1. Run an MQTT broker (the Mosquitto add-on) and the MQTT integration with
   discovery enabled (the default).
2. Dashboard → Settings → MQTT → enter broker host/credentials, save.
3. A **Window Controller** device appears in HA automatically:
   - a `cover` entity (open/close/stop) once codes named open/close exist
   - a `button` entity for every saved code
   - diagnostic sensors (RSSI, uptime)

The cover uses *assumed state* (there's no position feedback from the window),
so HA always shows both arrows.

MQTT topics (id = device id from the dashboard header):

| Topic | Direction | Payload |
|---|---|---|
| `windowctl/<id>/cmd/cover` | → device | `OPEN` / `CLOSE` / `STOP` |
| `windowctl/<id>/cmd/replay` | → device | code id or code name |
| `windowctl/<id>/rx` | ← device | JSON for every decoded signal heard (useful for automations triggered by the physical remote) |
| `windowctl/<id>/state` | ← device | JSON diagnostics every 30 s |
| `windowctl/<id>/status` | ← device | `online` / `offline` (LWT) |

## OTA updates

Settings → Firmware update. `Firmware` takes `.pio/build/lolin_c3_mini/firmware.bin`,
`Dashboard (filesystem)` takes `.pio/build/lolin_c3_mini/littlefs.bin`
(build it with `pio run -t buildfs`).

## Troubleshooting

- **Sniffer full of garbage / nothing decodes** — cheap regen receivers
  (XY-MK-5V) are noisy. Keep it away from the ESP32's own antenna, try 5V supply,
  check the antenna wire. If it stays unusable, swap in a superheterodyne RXB6.
- **Replay does nothing** — check transmitter antenna; bump the repeat count on
  the saved code; capture in Raw mode and replay that instead.
- **Codes that decode but don't actuate** can still work via raw capture — some
  remotes use timing the decoder approximates too loosely.
