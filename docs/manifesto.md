The official mind map for the ESP32 based dumbphone proof of concept.

Manifesto/Abstract:

As technology develops, normal people live increasingly at the whim of large corporations, conglomerates who connect to their clientele through a very specific physical medium: the smartphone. These panes of glass are a window unto the world, both a blessing and a curse.

By and large, people have embraced this paradigm shift with little resistance - after all, concessions must be made to exist in the modern world - concessions that seem innocuous but are actually very subversive: privacy, the right to disengage, even in some cases basic sanity. Still, progress marches onward, and in today's increasingly IoT connected world, it is impossible to opt out of the gateway. Governments, healthcare, communication, news, culture, entertainment, sandwiches - all of it needs the conduit that the smartphone has become, the second brain connected to all humanity.

Yet this invention was not just the child of necessity, because the specialization and standardization of the platforms smartphones utilize breed dependency: large companies have made decisions about what we see, how we see it, how often we connect, and in a sense, who we have become. We have accepted these concessions on the basis of inevitability and in allegiance to modernity and progress. I argue that the shape of this tool is not something we have to swallow or absorb simply in order to exist in the modern world: that we can make inherently more willful, healthy choices about how we use these tools, and it starts with putting the power back in the hands of the individual. It is not a contradiction in terms to say that to embrace technology properly, we must seek to control it, and not have it control us.

This is why I believe the necessity of the project I am undertaking: the adoption of a low cost, highly customizable digital tool - not necessarily a full window but a simple, small lens, purpose built and focused to what we actually want coming in, out, and surrounding us. I propose the return of the dumbphone, but for the modern age, in an affordable, customizable, modernized, and most importantly controllable package.

This requires all the tools within the system of technology as we know it today: voice recognition, backend servers, virtual machines, 4G networking, AI assisted tooling, modern communication apps - to be viable. But just because it exists within the machine doesn't mean it is evil or subversive - rather this approach attempts to embrace and guide the modern technological landscape to bring redemption.

The How:


## Concept (initial pitch — 7-min voice note, 24/11 ~15:45)

**Hardware:** Buy an ESP32 (~$10 on AliExpress) + a SIM module + a screen. In bulk, all parts ~$30 per unit. A 2005-era smartphone — except open source, running on well-understood, well-documented hardware: a chip that's been in the industry as long as anyone can remember. Not state of the art, but highly reliable, low power, and very cheap. That's the key one.

**The service:** The phone connects to APIs/adapters — internet servers we run — for a small subscription (~$1–2/month per user, on top of the one-time phone price). What it buys: access to a suite of applications — WhatsApp, Mail, Maps, Gmail, all the good stuff.

**The backend (the heart of it):** On our servers, versions of those applications run on real Android virtual machines (or web renditions of them). We scrape/relay that to the dumb phone. The phone never runs any high-power, complex application — it's just powerful enough to stream, not to run.

**The escape hatch:** If someone really needs another app — "they're at a sandwich place and they need the sandwich app" — they screen-share an Android VM on our server that runs it for them. This means we can provide ANY application they want, on a dumb phone.

**What we're selling:** The ability to genuinely break away from the system and still buy into all the applications you need to go about your life.

## Core services — what the phone does well

- WhatsApp — mostly solved: reverse-engineered WhatsApp protocols exist on GitHub (WAHA/Baileys). Get one running legally, make a couple tweaks to the stack, and the biggest app is done.
- Google apps — GWS is already really easy to access; you get a lot of Google's apps right there.
- Moovit / bus applications
- Banking (Israel) — probably buildable as your own simple app; needs research.
- SMS / calls / data — LTE modem (SIM7600 class).

## What it explicitly does NOT do

- Not building better front-ends than the people who built the actual device, for general apps.
- Niche-app escape hatch: VM screen-share as a third-party stopgap — "the odd case where I need this sandwich app" — offered as a subscription service. It helps break the device away from being a smartphone, but it is not what the device revolves around.

## Why the software is easier than it looks

- You're not building for Android. You're building a very simple application on very simple hardware — less buggy by design, way fewer complex parts to think about.
- Open source enough that people are interested in running it on their own.

## The OS shell — how it actually gets built (full plan: firmware-architecture.md)

- Not an OS/kernel project: ESP-IDF already ships SMP FreeRTOS, lwIP, mbedTLS, Wi-Fi/BLE, LittleFS/NVS, OTA. We build a desktop-style shell on top: event bus + background services + LVGL window manager.
- Four parts: (1) typed event bus — everything talks through messages, nothing calls directly; (2) Core 0 services — relay to backend, power/hardware, audio/camera; (3) one Core 1 UI task owning display+touch (LVGL); (4) apps as compiled C modules with a 4-function interface (init/on_open/on_event/on_close) — later, ELF apps from SD via espressif/elf_loader.
- Server-driven UI: one generic LVGL renderer taking tiny JSON layouts from our server — a new "app" is Python/JS on our side, no reflash.
- AI-coding guardrails: pin ESP-IDF+LVGL versions; all display updates cross a queue (LVGL is not thread-safe); big buffers go to PSRAM; no malloc in hot loops.
- Dev loop: build the whole shell against LVGL's SDL2 backend on Linux first (gcc + AddressSanitizer, 2-second compiles), then port by filling in small ESP-IDF HAL drivers; the agent closes the loop with idf.py build + monitor backtraces.

## Local harness — testing without silicon (full plan: harness-setup.md)

- IDE is a thin human viewport; the CLI toolchain is the platform: ESP-IDF (idf.py + CMake), editor via clangd on compile_commands.json, harness drives justfile targets — build-host / test-host / build-s3 / run-qemu / flash-s3.
- Three virtualization tiers behind a strict HAL: (1) native Linux + SDL2 — UI/WM/lifecycle/memory bugs at sub-second speed with ASan/Valgrind; (2) Espressif QEMU fork via idf.py qemu — real firmware, dual-core scheduling, panics, virtual LCD (esp_lcd_qemu_rgb); (3) Wokwi CLI + one tethered S3 board for what emulators miss (PSRAM bandwidth, RF coexistence, deep-sleep power).
- Test surface baked in from day one: JSON-RPC debug shell over UART (input injection, sys heap/tasks, ui dump_tree, sim battery/sms with mock AT commands), panic-hook wrapper that kills QEMU and saves decoded backtraces, framebuffer dumps for golden-image/vision diffs.

## Market — who it's for

- **The escape crowd:** people who want another phone instead of their smartphone, just to get away — like Light Phone does.
- **Haredim:** want a basic phone but want to control exactly what it has — Mail and their banking app, no pictures, simple, easy, customizable to exactly their community standards.
- **The counterculture / open-source builders:** people who want to build their own phone from scratch and use our service to do it. If we make it open source, they'll make modules for us, build the phone out, make it more viable. There's enough counterculture mentality around Big Brother technology nowadays to sell to a wide variety of people.
- **Retro / fun:** we can physically customize it, build all kinds of fun feature phones the world hasn't seen for years, retro phones, customizable OS.

## The genius — positioning

- Dumb phones died because software development was locked to the Android ecosystem. This is the go-between: between the big-brother subscription services of yore and one open-source account/subscription that gives you exactly how much power you want in your phone.
- Swiss army knife: what do you want the tool to be? How many pieces do you want on it?
- Android sold us the illusion that we should have the entire world in our pocket. The moment you start limiting things is when creativity starts coming out and things become cool again. Bring the potential of the smartphone back into a dumb-phone shape.

## Why now / why ESP32

- I'm getting into microcontrollers and low-power chips. You don't need a high-cost machine — a microcontroller less powerful than your average Raspberry Pi. Very cheap, very doable.
- Do what KaiOS did — except instead of designing an OS that navigates all these services from scratch, use tested servers and powerful technology to run snappy backends, so really cheap systems with RAM on the order of megabytes feel snappy and responsive for the modern age. Pared down, super retro, super basic.
- The ESP32 spin: viable, tested in the field, cheap, reliable. Every hobby project uses one. Easier for the builder (who knows nothing), easier for the people you open-source it to, easier to source as a part. RISC-V looks cool, but it has to get there.

## Honest scope (self-assessment)

- **Can do:** a rudimentary OS / proof of concept — buy an LCD, battery modules, 3D-printed housing. "Look, I built my own dumb phone. Is it good? No. Can I run WhatsApp with it? Yes. Is the concept there? Absolutely."
- **Cannot do alone:** a company-grade stack — multi-user, security-hardened, efficient, bug-free. This requires a hardware idea + a software idea + a finance idea to come together. It's bigger than one person.
- **Honesty about money:** it wouldn't draw a ton of profit — not everyone wants this — but there's a faithful enough base. Look at the Framework laptop: people in America, people in Israel, Haredim — everyone is looking for exactly this device. Making the technology complicated on OUR end, and putting the tool back in the user's hands.

## Motivation / narrative

**Torah based Values of space, balance, control, tzniut and self control built into and designed aroud the realiteis of a modern reshut harabim
- Local and independent computing. Legitimate options for normal people that aren't Android.
- A smartphone-free life without shooting yourself in the foot — no living a compromise.
- Control over your own technology, as opposed to a big company deciding what your technology should even look like.
- "We've been getting sold a very specific brand of tool for accessing the internet since its inception, and people never put their foot down." Interested in a fundamental paradigm shift.
- Open to concepts. Open to ideas.

## Prototype #1

 #Goals#
 proof of concept: we are dealing with wifi, not yet cellular: whatsapp reverse engineered on device, working screen, input interfaces. the os layer skeleton

 requirements:
https://github.com/waveshareteam/Waveshare-ESP32-components
https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5  (the specific board we ordered — firmware, ESP-IDF & Arduino examples; docs: https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5)

we are using the waveshare "Waveshare ESP32-S3 3.5inch Touch LCD Touchscreen Display N16R8 Development Board Support Wi-Fi Bluetooth" dev boardd, giving us everyhitng we need from the get go.

the first satge of prototyping will be building out the software/os side of things, creating a gui with a couple  apps and WAHA service backend integration for this device.

## The dev board purchase (voice note, 24/11 ~23:0x)

**Waveshare ESP32-S3 N16R8 3.5" dev board** (₪157.39) — the exact component set the budget-smartphone project needs: this exact screen, this exact microcontroller, this exact camera class. The cost of entry to hardware development — "more than just software, you can start to work with the hardware of things."

**The path forward:** buy the dev board ↦ run the AI models until happy with all the internal software ↦ design his own circuit board in KiCad ("key CAD") ↦ source each component as cheap as possible, build and ship the whole board himself ↦ a dumb phone he built himself — a prototype that could become the final term project (he may run it by some professors first). Even if it never becomes marketable: something to show what he's capable of, document the process online, "flex my muscles".

**Awaiting:** parents' approval + funds (checkout screenshot sent to Mind Meld 24/11).
