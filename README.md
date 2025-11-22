| Supported Targets | ESP32-S3 | ESP32-C3 | ESP32-C6 |
| ----------------- | -------- | -------- | -------- |

# 4-Relay Matter Accessory (Managed Components)

This firmware turns an ESP32 DevKit into a Matter accessory exposing **four independent On/Off relay endpoints** (e.g. to drive lamps, loads or dry contacts). It uses the `esp_matter` component pulled from the [Espressif Component Registry](https://components.espressif.com/) and integrates Arduino Core as an IDF component for a familiar high-level API surface.

The previous RGB light functionality has been replaced: each relay now maps to a Matter OnOff cluster endpoint (Endpoint 1..4). State changes are kept in sync bidirectionally between physical inputs (optional buttons) and any Matter controller (Google Home, Apple Home, Alexa, Home Assistant, etc.).

> Read the official [ESP-Matter Docs](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html) for environment setup, commissioning concepts and advanced customization.

## Features

- 4 independent relay GPIOs (configurable via `idf.py menuconfig`).
- Optional 4 local input buttons (short press toggles corresponding relay).
- Long press (factory reset) support on a designated button (configurable GPIO).
- Wi-Fi (2.4 GHz) networking; Thread (ESP32-C6 only) optional build.
- Persistent storage in NVS for Matter fabric info and relay states.
- Automatic recovery and re-announcement after reboot.

## Hardware Mapping

Default GPIO suggestions (adjust to your board & relay board design):

| Relay | Default GPIO (S3) | Default GPIO (C3/C6) | Menuconfig Symbol |
|-------|-------------------|----------------------|-------------------|
| R1    | 4                 | 4                    | RELAY_1_GPIO      |
| R2    | 5                 | 5                    | RELAY_2_GPIO      |
| R3    | 6                 | 6                    | RELAY_3_GPIO      |
| R4    | 7                 | 7                    | RELAY_4_GPIO      |
| FACTORY / Long Press Btn | 0 (S3)               | 9 (C3/C6)         | FACTORY_BTN_GPIO   |

Adjust these under: `idf.py menuconfig -> Matter 4-Relay Accessory` (exact path may vary based on `Kconfig.projbuild`).

If using active-low relay modules, the firmware can invert logic (toggle option in menuconfig).

## Commissioning

On first boot (or after factory reset) the device advertises itself for Matter commissioning.

Supported Controller Ecosystems:

- Amazon Alexa
- Google Home
- Apple Home
- Home Assistant (Matter add-on / SkyConnect / other bridges)


Testing VID/PID (example): VID `0xFFF1`, PID `0x8000`. If required by Google Home for test devices, register the virtual device in the [Google Home Developer Console](https://developers.home.google.com/codelabs/matter-device#2) before commissioning.

**Manual setup code:** `34970112332` (Use manual code entry if no QR is provided). Replace this in source if you have production credentials.

## Runtime Behavior

- Each relay endpoint reports its OnOff state; controllers can toggle any subset.
- Physical button press (short) toggles and immediately publishes new state.
- Long press (>10 s) on factory button triggers full reset (Matter + Wi-Fi) then auto-reboot into commissioning mode.
- Status LED (optional) can be enabled to show states (pattern definitions TBD / user customizable). If you still have a built-in RGB LED you may repurpose colors (e.g., Purple = uncommissioned, White = commissioned & online, Orange = Wi-Fi pending). This is optional; remove if not needed.

## Building (Wi-Fi + Matter)

Use ESP-IDF 5.1.4 (tested) and Arduino Core 3.0.4 (via managed components).

Clean & build sequence (Linux/macOS example for ESP32-S3):

```bash
rm -rf build
idf.py set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults" -p /dev/ttyACM0 flash monitor
```

Windows (ESP32-C3 example):

```bat
rmdir /s/q build
idf.py set-target esp32c3
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults" -p com3 flash monitor
```

If you switch targets or menuconfig options significantly, delete managed state:

```bash
rm -rf build managed_components sdkconfig dependencies.lock
```
 
Windows:

```bat
rmdir /s/q build managed_components && del sdkconfig dependencies.lock
```

## Building (Thread + Matter, ESP32-C6 Only)

Requires a Thread Border Router active in your environment (e.g., Home Assistant OTBR, Google/Apple Thread-capable hubs).

Example (Linux/macOS):

```bash
rm -rf build
idf.py set-target esp32c6
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.c6_thread" -p /dev/ttyACM0 flash monitor
```
 
Windows:

```bat
rmdir /s/q build
idf.py set-target esp32c6
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.c6_thread" -p com3 flash monitor
```

Clean state if needed (same folders as Wi-Fi build).

## Menuconfig Options (Summary)

Navigate with `idf.py menuconfig`:

- Relay GPIOs (1..4)
- Relay active level (HIGH/LOW)
- Factory reset button GPIO & long-press duration
- Optional status LED enable & pin
- Enable/disable Thread (C6) or keep Wi-Fi only
- Commissioning test credentials / custom VID/PID

## Factory Reset Procedure

Press and hold the configured factory button for >10 seconds, release: device erases NVS entries for Matter + Wi-Fi, reboots into commissioning mode. Remove old virtual accessory from controller app before recommissioning (previous fabric association is invalid after reset).

## Extending

Ideas:

- Map relays to different Matter device types (Plug-in Unit vs Switch) by changing endpoint device type in initialization.
- Add power measurement clusters if hardware supports current/voltage sensing.
- Implement schedule or scene support via additional clusters.

## Troubleshooting

- Ensure 2.4 GHz Wi-Fi (no 5 GHz only SSID).
- Verify relay board voltage & logic level compatibility (use transistor/optocoupler modules if required).
- If commissioning stalls: clear build artifacts, factory reset, power cycle, retry.
- Thread builds require correct SDKCONFIG defaults; confirm inclusion of `sdkconfig.defaults.c6_thread`.

## License / Credentials

Firmware currently uses test Matter credentials. Replace with production credentials for commercial deployment (follow CSA + Espressif guidance). Remove or change manual setup code accordingly.

---
Feel free to adjust GPIO defaults and sections to better match your exact 4-relay hardware. Open an issue / discussion for enhancements or clarifications.
