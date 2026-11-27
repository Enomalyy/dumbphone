# Firmware architecture — the on-device OS shell

Distilled from Eli's 24/11 essay on AI-assisted ESP32-S3 firmware development.
Companion to `ESP32 phone.txt` (the mind map) — that file is *what we're building*,
this one is *how it gets built*.

## The reframe: a shell, not an OS

"Building an OS" sounds like writing virtual-memory managers and CPU schedulers from scratch.
On an ESP32-S3 the scary kernel engineering is already done inside ESP-IDF. What we're
actually building is a clean, structured C/C++ **desktop-style shell** — a window manager,
a few background worker threads, and an event bus — sitting on top of an existing RTOS.
Dramatically smaller than a smartphone OS, and well inside the wheelhouse of AI-assisted coding
*if* architectural guardrails are enforced up front.

## What ESP-IDF already provides (don't rebuild these)

- **Kernel:** SMP FreeRTOS — scheduling, preemption, mutexes, queues across Core 0 + Core 1
- **Networking/storage:** lwIP TCP/IP, mbedTLS, Wi-Fi, BLE, LittleFS/NVS wear-leveling,
  SD-card filesystems, OTA updates with rollback

## The four parts of the Stage-1 shell

1. **Event bus** (`esp_event` / FreeRTOS queues) — everything talks through typed messages:
   `EVENT_WHATSAPP_MSG_RX`, `EVENT_BATTERY_LOW`, `EVENT_WIFI_CONNECTED`. One pattern keeps
   the codebase sane as it grows; components never call each other directly.
2. **Background services** — FreeRTOS tasks pinned to **Core 0**; a "daemon" is just a C
   function in an infinite loop, sleeping until data arrives:
   - *Relay/network:* holds the WebSocket/MQTT link to our backend, parses WhatsApp/Moovit
     payloads, saves to flash/SD, posts events to the bus
   - *Hardware/power:* polls PMIC/battery fuel gauge, backlight auto-dim, physical buttons,
     light/deep sleep
   - *Audio/camera:* wakes on demand — voice note into PSRAM, photo into PSRAM
3. **UI & window manager** — a single task pinned to **Core 1** running LVGL. Owns the display
   and touch controller *exclusively*. Draws the status bar (time/Wi-Fi/battery), swaps the
   foreground app, slides down notification toasts when the bus wakes it.
4. **Apps** — v1: no dynamic loading. An "app" is a C/C++ module compiled into the firmware
   implementing a 4-function interface: `init()`, `on_open(lv_obj_t *root)`, `on_event(evt)`,
   `on_close()`. Later ecosystem: Espressif's official `espressif/elf_loader` runs compiled
   ELF binaries straight from PSRAM — third parties drop apps onto the SD card, no reflash.

## Runtime architecture — cores by criticality, pages vs applications

- **Split the cores by criticality, not by feature.** One core runs LVGL + the input drain at high priority and is never allowed to block: it takes events off queues and returns. The other core runs everything that can block — the modem's AT state machine, network to the server, image decode, audio. The HAL already separates `modem`/`net`/`audio` from `display`/`input`; treat that separation as the contract for this split, not a convenience.
- **Keep the UI resident.** Never unload it to make room for other work: the panel's owner can change, and the UI is suspended, not destroyed — rebuilding LVGL state on every handoff costs frames and invalidates object pointers.
- **Drive the modem as an event, never poll it in a loop.** AT over UART, a task blocked on serial events, data through a ring buffer. Voice calls are cheaper than they look: the module owns the audio path and the ESP32 routes PCM rather than encoding it. Server sync is push or a long-lived connection, never a timer — a timer keeps the radio awake, and the radio is where the power goes.
- **Decode images and audio off the drawing core.** The S3 has no hardware JPEG decoder; a 320×480 decode costs tens of milliseconds of one core, so run it on the non-UI core into a PSRAM buffer and hand the UI a ready pointer.
- **Draw the page/application boundary as a resource question.** A *page* shares the frame: same renderer, same status/softkey bars, same keymap vocabulary, one row in `ui_pages[]`. An *application* takes the panel and saturates a core. Messaging/WhatsApp is therefore pages — it shares the frame and does not need a core; an emulator is an application.
- **Carry foreign ESP32 software two ways, both real.** (a) A separate firmware image in flash, chosen at boot — a reboot and a slot swap. (b) An app slot inside the phone image — it takes the panel and one core, the modem and network keep running on the other, the UI is suspended, and the page is rebuilt on return.
- **Budget feasibility with numbers; Game Boy is the worked example.** GB emulation on ESP32-S3 is solved: Peanut-GB is the common core, CrankBoy (a tuned fork) reaches ~60 fps with dynamic frame skipping, and at least one published project gives the second core to display updates. Budget: ~20–30 KB of code, ROMs from flash or SD, ~150–200 KB of hot state — tight against internal RAM, workable if the UI's working set lives in PSRAM. Flash is not the constraint (16 MB; the UI uses a fraction); **RAM is the number to watch**. Game Boy Advance / SNES: not a bet worth taking. Cite these as community results, not measurements of ours.
- **Rely on the cheap-rebuild property for (b).** A page can be destroyed and rebuilt because a page is data plus a registry row (spec §16.3) and its data lives in the model layer (spec §16.4) — that is the payoff of those two stages.

## Server-driven UI shortcut

For simple services (transit, weather, basic forms) we don't need a compiled C app at all:
**one generic LVGL renderer** that accepts tiny JSON/binary layout descriptions from our server
(lists, text inputs, buttons, images). Adding a whole new "app" = writing Python/JS on the server.

## AI vibe-coding failure modes on bare metal (why the guardrails exist)

Letting an agent free-code an ESP-IDF repo unchecked produces code that compiles cleanly and
reboots three minutes later with a Guru Meditation Error. The failures are predictable:

- **Thread-safety:** LVGL is *not* thread-safe. Models write network callbacks on Core 0 that
  directly update labels on Core 1 — no mutex, no queue. Works half the time, corrupts memory
  the other half.
- **Memory/stack blindness:** a 10 KB local array overflows a 4 KB task stack instantly;
  forgetting `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for big buffers quietly starves the
  internal SRAM that Wi-Fi and TLS need.
- **Hallucinated API drift:** ESP-IDF shifted APIs across v4/v5/v6 (I2C driver, LCD panel API,
  LVGL v8 vs v9). Without pinned versions + compiler feedback in the loop, models mix versions.

## Harness rules — before line one of app code

1. **Linux desktop simulator target first (biggest multiplier).** LVGL has an official SDL2
   backend. Put a thin HAL (`hal_display`, `hal_net`, `hal_storage`, `hal_audio`) underneath the
   phone OS so ~85% of the codebase — window manager, every app, the relay protocol, the event
   bus — compiles natively on Linux with gcc in two seconds. The AI harness then compiles, unit
   tests, inspects UI behavior, and catches leaks/OOB writes with AddressSanitizer — no USB cable.
2. **Rigid architectural rules in the project spec.** Pin ESP-IDF + LVGL versions explicitly;
   ban direct LVGL calls from outside the UI task (everything crosses a queue); ban dynamic
   malloc inside hot loops; require PSRAM allocators for buffers over a few KB. A clean, tiny
   HAL boundary turns embedded engineering back into ordinary modular C/C++.
3. **Serial loop for real hardware.** Once the shell runs on desktop, porting to the Waveshare
   S3 board is just filling in the small ESP-IDF HAL drivers. The agent invokes `idf.py build`,
   reads exact compiler errors, inspects decoded backtraces from `idf.py monitor` — models are
   good at fixing real stack traces once they see them.

## Proof this pattern works in the wild

- **Tactility** — active open-source ESP32-S3 OS: LVGL, system services, SD-card ELF apps
- **espressif/elf_loader** — official Espressif component for loading ELF binaries from PSRAM

## Bottom line

Keep the firmware thin, isolate hardware behind a HAL, do most iteration in the desktop SDL
simulator, let the relay server absorb the messy internet logic. The actual microcontroller code
ends up small and clean enough that an AI-assisted workflow handles it remarkably well.
