# Local harness — testing the ESP32-S3 OS without silicon

Distilled from Eli's 24/11 essay on the local test harness for the dumb-phone firmware.
Companion to `firmware-architecture.md` (what the OS is) — this one is *how we verify it*.

## Core principle

The biggest trap is tying the build/test workflow to a GUI IDE. **Treat the IDE as a thin
human viewport; the CLI toolchain + virtualization layer is the real platform.**

## 1. Decouple the IDE from the build system

- Foundation: **ESP-IDF** (`idf.py` + CMake), not Arduino IDE or an IDE-locked format.
  (PlatformIO CLI is viable too, but ESP-IDF gives direct access to Espressif's QEMU
  integration, FreeRTOS internals, and host-target tooling.)
- **For humans:** any editor (VS Code + ESP-IDF extension, Neovim, Zed) pointed at the
  generated `compile_commands.json` via clangd.
- **For the harness:** every operation exposed as a deterministic CLI target (justfile /
  Makefile / small Python runner): `build-host`, `test-host`, `build-s3`, `run-qemu`,
  `flash-s3`. The harness never touches the IDE — it invokes the same commands the editor
  runs under the hood.

## 2. Three-tier virtualization stack

Emulating everything at register level on every edit is slow and brittle (display, touch,
audio, eventually a modem). Split the stack behind a clean BSP/HAL boundary so ~90% of the
OS tests without flashing silicon:

| Tier | Runtime | Tests | Speed / fidelity | Blind spots |
|---|---|---|---|---|
| 1. Native host | Linux binary (SDL2 + POSIX FreeRTOS / mocks) | WM, UI (LVGL), app lifecycle, IPC, filesystem logic, memory bugs (ASan/Valgrind) | Sub-second rebuilds, native CPU speed | Xtensa compiler quirks, IRAM/PSRAM limits, real DMA/ISR timing |
| 2. QEMU | Espressif's `qemu-system-xtensa` (`-machine esp32s3`) | Real compiled firmware, partition tables, dual-core FreeRTOS scheduling, bootloaders, panics, virtual LCD | Seconds; true Xtensa LX7 emulation | No real RF (Wi-Fi/BLE PHY), no cellular modem, limited custom SPI/I2C unless patched |
| 3. Peripheral sim / HIL | `wokwi-cli` or a tethered USB board | Real SPI/I2C display drivers, touch ICs, GPIO wiring, Wi-Fi/BLE stack, power/sleep | Slower; closest to/actual silicon | Wokwi needs a CI token/cloud runner; real board needs physical resets/wiring |

### Tier 1 — native host (daily driver for OS & UI)

Isolate hardware behind thin interfaces (`hal_display`, `hal_input`, `hal_audio`,
`hal_modem`, `hal_storage`) and the whole upper OS compiles natively:

- **ESP-IDF Linux target:** `idf.py --preview set-target linux` runs FreeRTOS over a
  POSIX/Linux simulator alongside CMock/Unity unit tests. *Experimental, single-core;
  POSIX signal-based scheduling has caveats (e.g. non-async-signal-safe `printf()` across tasks).*
- **UI/compositor:** LVGL + SDL2 backend as a native process unlocks AddressSanitizer,
  UBSan, Valgrind — catches heap corruption / use-after-free in seconds that would
  otherwise surface as cryptic `LoadProhibited` panics on the S3.

### Tier 2 — Espressif's QEMU fork (full firmware virtualization)

Official QEMU fork with ESP32-S3 support integrated into `idf.py` — the sweet spot for
testing the actual compiled image before hardware:

- `idf.py qemu monitor` — compiles, builds the flash image, boots QEMU, streams UART with
  automatic panic-backtrace decoding
- `idf.py qemu --graphics monitor` — virtual framebuffer; pair with Espressif's
  **`espressif/esp_lcd_qemu_rgb`** component, which plugs into the standard `esp_lcd` API
  so display code barely changes between QEMU and real hardware
- Headless: `qemu-system-xtensa -nographic`, UART to TCP port or stdio, GDB attach
  (`idf.py qemu --gdb`), eFuse + partition-layout tests (2/4/8/16 MB images merged via
  `esptool.py merge_bin`)
- Networking: real projects in QEMU (e.g. **EspOS**) swap Wi-Fi for QEMU's emulated
  OpenCores Ethernet with host NAT — TLS, sockets, app-sync stacks run unmodified

### Tier 3 — Wokwi CLI + tethered hardware gate

- **`wokwi-cli`** for peripheral-level testing: real ST7789/ILI9341 SPI displays, buttons,
  SD cards, custom C-coded virtual chips (Chips API). Headless automation, display
  screenshot capture, GDB, and an experimental `wokwi-cli mcp` server over stdio designed
  for coding agents. Tradeoff: needs a Wokwi CI token, not purely local/offline.
- **Final hardware gate:** keep one ESP32-S3 on USB solely for what no emulator gets right —
  PSRAM bandwidth bottlenecks, SPI DMA tearing, real RF/Wi-Fi/BLE coexistence, modem UART
  flow control, deep-sleep power draws. **`pytest-embedded`** automates flashing + serial
  parsing when a PR/milestone is ready for silicon.

## 3. Test surface — baked into the OS from day one

An automated harness is only as good as the feedback loop. Don't make it parse messy logs or
guess what's on screen:

- **Structured debug shell over UART/TCP:** a command/JSON-RPC listener on UART0 (or a
  dedicated virtual UART in QEMU) so the harness drives the OS without simulated mouse clicks:
  - Input injection: `input touch <x> <y>`, `input key <home|back|power>`
  - State inspection: `sys heap`, `sys tasks`, `ui dump_tree` (active views/widgets + bounds as JSON)
  - Simulated hardware events: `sim battery 15`, `sim sms "+123456" "hello"` — feed mock AT
    commands to the telephony daemon over a virtual UART *before* any cellular module exists
- **Deterministic exit & panic hooks:** wrap `qemu-system-xtensa` in a runner script with a
  hard wall-clock timeout; watch UART for `Guru Meditation Error`, `Backtrace:`, watchdog
  resets. On hit: kill QEMU, save the decoded stack trace, return non-zero — the harness
  never hangs on a wedged emulator.
- **Framebuffer snapshots:** dump the current framebuffer (SDL2 in Tier 1, virtual RGB in
  Tier 2) to raw/PNG on disk. Enables golden-image diffs — or passing frames to a vision
  model — to verify layout regressions without a webcam on a dev board.

## Starting point

ESP-IDF + strict HAL boundary, with **Tier 1 (native Linux + SDL2)** and **Tier 2
(`idf.py qemu` + `esp_lcd_qemu_rgb` + TCP UART control port)** wired up first: a fast, fully
offline testbed where every OS iteration is verified in seconds before it ever hits flash.
