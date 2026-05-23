ESP-IDF Hello World for Waveshare 1.75" Round AMOLED (placeholder)

Overview
- Minimal ESP-IDF project that initializes SPI and calls placeholder GC9A01 routines.

Files added
- `main/main.c` - app entry initializing SPI and calling display routines.
- `main/display_gc9a01.h` / `.c` - minimal adapter with placeholder functions.

Before you build
- Install ESP-IDF and set up the environment per Espressif docs.

Configure pins
- Edit `main.c` to match your board's SPI pins (MOSI, SCLK, CS, DC, RST, BL).

Build and flash (PowerShell)
```powershell
cd c:\Users\TetSigma\sample_project
.
# set ESP-IDF env (example)
.
# Build
idf.py build

# Flash to COM3 (adjust target/port per your setup)
idf.py -p COM3 flash
```

Notes
- The display driver initialization and pixel drawing in `display_gc9a01.c` are placeholders. If you want, I can fetch Waveshare's exact init sequence for the GC9A01 controller and implement pixel streaming and text rendering.
