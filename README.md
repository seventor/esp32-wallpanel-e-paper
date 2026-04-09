# ESP32 e-paper VG news

Firmware for the [Waveshare E-Paper ESP32 Driver Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board) that loads [VG’s RSS feed](https://www.vg.no/rss/feed/?format=rss) over WiFi and shows the **10 latest headlines** on a connected Waveshare SPI e-paper panel.

---

## Windows vs macOS — what we learned

| Topic | Windows | macOS |
|--------|---------|--------|
| **USB serial upload** | Often works without extra steps. Device appears as a **COM port** in Device Manager / Arduino IDE. | Often needs **USB serial drivers** (CH343 or CP210x, depending on the board). Device appears as **`/dev/cu.…`** (use **`cu`**, not `tty`). |
| **Typical upload pain** | Usually low. | **“Failed to write to target RAM”**, timeouts, or flaky uploads — USB stack / drivers / timing. |
| **Mitigations on macOS** | — | Install/update **WCH CH343** or **Silicon Labs CP210x** driver; use **direct USB port**; lower **`upload_speed`** in `platformio.ini`; enable **`upload_flags = --no-stub`** under `[env:esp32_epaper]`; close Serial Monitor before upload. |
| **Python / PlatformIO** | Install PlatformIO from installer or pip. | Homebrew Python may block `pip install` globally (**PEP 668**). Use a **venv** (`python3 -m venv .venv` → `.venv/bin/pip install platformio`) or **`pipx install platformio`**. |

**Practical split that worked:** use **Windows + USB** when you need a **reliable first flash** (or any time USB serial must work). Use **macOS** (or Windows on the same LAN) to **build** and to **deploy over WiFi (OTA)** once the board already has WiFi and an OTA-capable sketch on it.

---

## End-to-end workflow (OTA after BasicOTA on Windows)

This is the flow that worked: get the board **on WiFi with Arduino’s BasicOTA** from Windows, then push **this project** over the network so you never depend on macOS USB for the big firmware.

### 1) On Windows — put WiFi + OTA on the chip (Arduino IDE)

1. Install [Arduino IDE](https://www.arduino.cc/en/software) and the **Espressif ESP32** board package (Additional Board URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`).
2. **File → Examples → ArduinoOTA → BasicOTA**.
3. Set **SSID** and **WiFi password** in the sketch. Note whether you use **`ArduinoOTA.setPassword("...")`** — you will match that password later in this project’s `.env` (`OTA_PASSWORD`).
4. **Tools → Board → ESP32 Dev Module**, pick the **COM** port, choose a partition scheme with **OTA** (e.g. default schemes that include two app slots).
5. **Upload**, then **Serial Monitor** (usually **115200** baud) and note the **IP address** (e.g. `192.168.2.2`). The board is now on WiFi and listening for OTA.

### 2) In this repo — configure WiFi and OTA password

1. Copy **`.env.example`** to **`.env`** and set:
   - **`WIFI_SSID`** / **`WIFI_PASSWORD`** — same network the ESP32 already uses.
   - **`OTA_PASSWORD`** — use the **same string** as:
     - **`ArduinoOTA.setPassword(...)`** in BasicOTA **if** you set one, **or**
     - a new secret you choose — then flash this project once OTA with that password.
   - Optional: **`ESP32_OTA_IP`** to override OTA target IP at upload time.

2. **`platformio.ini` → `[env:esp32_epaper_ota]`:**
   - `scripts/load_env.py` injects `--auth=${OTA_PASSWORD}` from `.env`.
   - `upload_port` can be left as default or overridden by `.env` (`ESP32_OTA_IP`).

### 3) Build and OTA-upload this project (Mac or Windows, same LAN)

From the project folder (venv active if you use one):

```bash
pio run -e esp32_epaper_ota -t upload
```

Requirements:

- PC and ESP32 on the **same subnet** (same WiFi/LAN).
- Firewall allows the OTA connection to the ESP32.

After success, the board runs **VG RSS + e-paper**; future updates can use **`esp32_epaper_ota`** again (password is then whatever **`OTA_PASSWORD`** is in firmware).

### 4) If OTA to replace BasicOTA fails

Different **partition tables** (Arduino “Default” vs this project’s **`min_spiffs.csv`**) can block OTA from one sketch to another. **Fallback:** flash **once** over **USB on Windows**:

```bash
pio run -e esp32_epaper -t upload
```

Then use **`esp32_epaper_ota`** for later updates.

---

## Run / build locally (PlatformIO)

### Install PlatformIO (macOS note)

If `pip install platformio` fails with **externally-managed-environment**, use a venv or pipx:

```bash
python3 -m venv .venv
.venv/bin/pip install platformio
# then: .venv/bin/pio ...
```

Or: `brew install pipx && pipx install platformio`.

### macOS: get `pio` working in this repo

Run this once from the project root:

```bash
cd /path/to/esp32-wallpanel-news
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install platformio
pio --version
```

Use `source .venv/bin/activate` in each new terminal before running `pio ...`.

If you do not want shell activation, use the full path:

```bash
.venv/bin/pio run -e esp32_epaper
.venv/bin/pio run -e esp32_epaper_ota -t upload
```

### WiFi and secrets (`.env`, recommended)

Use a local `.env` file in the repo root (ignored by git) and let PlatformIO inject values at build time.

1. Copy the template:

```bash
cp .env.example .env
```

2. Edit **`.env`** with your real values:

```env
WIFI_SSID="your-2.4ghz-ssid"
WIFI_PASSWORD="your-wifi-password"
OTA_PASSWORD="your-ota-password"
# ESP32_OTA_IP="192.168.2.2"   # optional, for OTA env
```

3. Build/upload as normal (`pio run ...`).  
   `scripts/load_env.py` reads `.env` and:
   - adds `WIFI_SSID`, `WIFI_PASSWORD`, `OTA_PASSWORD` as compile-time defines
   - for `esp32_epaper_ota`, updates `--auth=` from `OTA_PASSWORD`
   - optionally overrides OTA `upload_port` from `ESP32_OTA_IP`

Do not commit real passwords to a public repository.

### Environments in `platformio.ini`

| Environment | Use |
|-------------|-----|
| **`esp32_epaper`** | USB serial upload (`pio run -e esp32_epaper -t upload`). |
| **`esp32_epaper_ota`** | Network upload to IP in **`upload_port`** (`pio run -e esp32_epaper_ota -t upload`). |

Default **`pio run`** builds the **first** `[env:…]` in the file; specify **`-e`** explicitly to avoid ambiguity.

### USB upload (serial)

```bash
pio run -e esp32_epaper -t upload
pio device monitor
```

Match **`monitor_speed`** in `platformio.ini` to **`Serial.begin(...)`** in `src/main.cpp` (default **115200**).

**Hardware:** Waveshare **UART switch ON**, correct **e-paper mode switch**, [BOOT button](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board#FAQ) if upload stalls on “Connecting…”.

### macOS USB troubleshooting checklist

1. **`/dev/cu.*`** port selected (not `tty` only).
2. **CH343 / CP210x** driver installed or updated.
3. **No other app** using the serial port (monitor closed).
4. In **`[env:esp32_epaper]`**: try **`upload_speed`** 57600 or 115200; enable **`upload_flags = --no-stub`** if you see stub/RAM errors.
5. **PlatformIO IDE** stuck on “Initializing”: install Core via venv/pipx and set **PlatformIO: Custom PATH** to the folder containing **`pio`**.

---

## Match your e-paper panel

Edit **`include/display_selection.h`**: set the **`#include`** under **`epd/`** (or **`gdey/`**) and **`#define EPD_DRIVER_CLASS`** to match your **exact** panel SKU.

The **default currently selected in this repo** is **7.5″ HD b/w 800×480** using **`GxEPD2_750_GDEY075T7`** (set by `USE_GDEY075T7_DRIVER 1` in `include/display_selection.h`).  
If your panel is from the other 7.5″ HD controller family, switch to **`GxEPD2_750_T7`** (`USE_GDEY075T7_DRIVER 0`). These two are the same size/resolution but **not interchangeable**.

### Screen model resources

- Waveshare ESP32 driver board support/models: [Waveshare Wiki: E-Paper ESP32 Driver Board](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board)
- GxEPD2 library (panel classes and examples): [GxEPD2 on GitHub](https://github.com/ZinggJM/GxEPD2)
- Current default panel family in this repo:
  - [Good Display GDEY075T7 (UC8179, 800×480)](https://www.good-display.com/product/396.html)
  - GxEPD2 class: `GxEPD2_750_GDEY075T7`
- Alternative 7.5″ HD family often sold as Waveshare “7.5 inch”:
  - [Good Display GDEW075T7 (GD7965, 800×480)](https://www.e-paper-display.com/products_detail/productId%3D456.html)
  - GxEPD2 class: `GxEPD2_750_T7`
- Older 7.5″ panel option (different resolution): `GxEPD2_750` (640×384)

If the image is blank/garbled or goes white after update, verify the exact panel label/FPC code and match the class in `include/display_selection.h`. **3-color** (b/w/red) panels need **`GxEPD2_3C`** drivers, not **`GxEPD2_BW`**.

---

## Behaviour

- Boot: WiFi → HTTPS GET of the VG RSS URL → up to 10 `<item>` titles → draw on e-paper.
- **`ArduinoOTA.handle()`** runs in **`loop()`** so OTA works between RSS refresh intervals.
- HTTPS uses **`setInsecure()`** for simplicity.
- RSS refresh interval: **`kRefreshMs`** in **`src/main.cpp`** (default 15 minutes).

### Fonts (Google Noto Sans, Norwegian letters)

The display stack (**U8g2**) uses **pre-rendered bitmap fonts** (C arrays), not TrueType/OpenType files. **Google Fonts** (e.g. **Noto Sans**) ship as **.ttf**; they must be converted with **`otf2bdf`** + **`bdfconv`** from the [u8g2](https://github.com/olikraus/u8g2) project. This repo includes **`src/noto22.c`**, **`src/noto28.c`**, **`src/noto36.c`** (Noto Sans, **SIL Open Font License**) with a **Latin-1 + ASCII** subset so **ÆØÅ** render correctly at scalable pixel sizes. To regenerate after changing point sizes or the character map, run **`scripts/generate_noto_u8g2.sh`** (requires **curl**, **otf2bdf**, **unzip**, **make**, **gcc**).

### If the panel flashes for a long time then ends white (large / 7.5″ panels)

- Each **full refresh** runs a long **white/black blink sequence** — that is **normal** for e-paper before the final image appears.
- **Too many full refreshes in a row** (WiFi text, then RSS text, then news) can **brown out** USB power: the ESP32 resets mid-update and the panel can stay **white**. The firmware is written so **WiFi and RSS fetch happen first** (status only on **Serial**), then **`initEpd()` once** and **one** full draw with the headlines — **fewer** refresh cycles.
- **Power:** a **7.5″** panel needs more current during refresh. Prefer the board’s **5 V** input with a **≥ 1 A** supply, not only laptop USB.
- **Serial monitor** at boot shows **`Reset reason:`** — **`9` = BROWNOUT** means undervoltage.

---

## Project layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | RSS fetch, XML titles, e-paper UI, ArduinoOTA |
| `src/noto{22,28,36}.c` | U8g2 bitmap fonts (Noto Sans, generated) |
| `include/font_noto_sans_u8g2.h` | Declarations for the Noto fonts |
| `include/display_selection.h` | GxEPD2 panel driver |
| `platformio.ini` | `esp32_epaper` (USB), `esp32_epaper_ota` (WiFi IP + `--auth`) |

---

## Licence

Use and modify freely for personal projects; respect VG’s terms of service for the RSS feed.
