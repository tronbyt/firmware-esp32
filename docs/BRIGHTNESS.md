# Brightness & Night Mode: Server↔Firmware Contract and Per-Board Handling

> Status: **implemented for Gen1/Gen2** (branch `fix/per-board-brightness-ceiling`).
> Gen1 and Gen2 now use a `BRIGHTNESS_8BIT_MAX` of 100 (Tidbyt-accurate 1:1);
> third-party boards (S3, Pixoticker, MatrixPortal, Waveshare) keep the legacy
> 230 fallback pending empirical tuning. This documents *why* tronbyt devices
> looked brighter than a real Tidbyt at the same setting, and how the per-board
> fix works (the right place for the S3).

## TL;DR

- The server and firmware **agree on the scale**: brightness travels as a raw
  integer **0–100** (a percentage). This is *not* a unit/format mismatch.
- The discrepancy is entirely in the **firmware's percent→8-bit conversion**.
  `brightness_percent_to_8bit()` multiplies the 0–100 value up onto a 0–**230**
  range (`×2.3`). The reference Tidbyt firmware (the public **HDK**) feeds the
  0–100 number **straight into `setBrightness8()`** with no scaling.
- Net effect: a tronbyt device is **~2.3× brighter than a real Tidbyt** at every
  level, and its ceiling is more than double (≈90% panel duty vs ≈39%). At the
  dim/night end, where perception is roughly logarithmic, that 2.3× reads as
  "off by orders of magnitude."
- **Night mode lives entirely on the server.** The firmware has no night/dim
  logic at all — it just receives an already-resolved brightness number. So the
  "night mode is different" symptom is the *same* `×2.3` bug applied to the
  night brightness value.
- **The fix belongs in the firmware, not the server.** The server sends a
  hardware-agnostic 0–100 — correct for every board it supports. Per-device
  translation to panel duty is exactly what `display.cpp` is for. Board type is
  already a **compile-time** selection, so per-board brightness math is a clean,
  isolated change that matches the existing code structure.

---

## 1. The contract: how brightness is sent and translated

### Server side — sends raw 0–100

The server resolves a single effective brightness (folding in night/dim state)
and writes it verbatim into an HTTP header / WS message:

```go
// tronbyt-server: internal/server/handlers_device_api.go:139-141
brightness := device.GetEffectiveBrightness()
w.Header().Set("Tronbyt-Brightness", fmt.Sprintf("%d", brightness))
```

```go
// internal/data/models.go:922
func (d *Device) GetEffectiveBrightness() int {
    brightness := int(d.Brightness)                // 0–100 percentage
    if d.GetNightModeIsActive() {
        brightness = int(d.NightBrightness)
    } else if d.GetDimModeIsActive() && d.DimBrightness != nil {
        brightness = int(*d.DimBrightness)
    }
    return brightness
}
```

`Brightness` is a 0–100 percentage (the DB field is literally commented
`// 0-100`, gorm default 20). The type *has* a `Uint8()` (0–255) helper, but it
is **never called anywhere in the server** — confirmed by grep. The websocket
path (`websockets.go:270`) sends the same raw 0–100 as JSON `{"brightness": N}`.

The UI exposes 6 levels (0–5) mapped to stored percentages
(`BrightnessFromUIScale`, `models.go:313-320`):

| UI level | Stored % |
|---------:|---------:|
| 0 | 0 |
| 1 | 3 |
| 2 | 5 |
| 3 | 12 |
| 4 | 35 |
| 5 | 100 |

These percentages, and the HDK constants (`DEFAULT 30`, `MAX 100`), live in the
**same 0–100-as-8-bit domain** — i.e. they were chosen assuming the firmware
passes the value straight to `setBrightness8()`.

### Firmware side — receives 0–100, then inflates ×2.3

```c
// main/remote.c:73-76  (HTTP header path)
if (strcasecmp(event->header_key, "Tronbyt-Brightness") == 0) {
    state->brightness = (uint8_t)atoi(event->header_value);  // API spec: 0-100
}
// main/remote.c:270
*brightness_pct = state.brightness;  // "Assumes API provides 0–100 as spec'd"  ✅ correct
```

```c
// main/main.c:206-214  (WS / JSON path) — also clamps to 0–100
cJSON* brightness_item = cJSON_GetObjectItem(root, "brightness");
if (cJSON_IsNumber(brightness_item)) {
    int brightness_value = brightness_item->valueint;
    if (brightness_value < DISPLAY_MIN_BRIGHTNESS) brightness_value = DISPLAY_MIN_BRIGHTNESS;
    if (brightness_value > DISPLAY_MAX_BRIGHTNESS) brightness_value = DISPLAY_MAX_BRIGHTNESS; // 100
    display_set_brightness((uint8_t)brightness_value);
}
```

```c
// main/display.cpp:205-209  ← THE DIVERGENCE
static inline uint8_t brightness_percent_to_8bit(uint8_t pct) {
    if (pct > 100) pct = 100;
    return (uint8_t)(((uint32_t)pct * 230 + 50) / 100);  // 230 as MAX 8 BIT HARDCODED
}
```

So the 0–100 contract is honored on the way in, then the value is remapped onto
0–230 instead of 0–100.

### The numbers

`setBrightness8(x)` sets panel PWM duty as `x/255`.

| Server sends (%) | HDK / real Tidbyt → `setBrightness8` | % panel duty | tronbyt → `setBrightness8` (`×2.3`) | % panel duty |
|----------------:|-------------------------------------:|-------------:|------------------------------------:|-------------:|
| 3 (UI 1) | 3 | 1.2% | 7 | 2.7% |
| 5 (UI 2) | 5 | 2.0% | 12 | 4.7% |
| 12 (UI 3) | 12 | 4.7% | 28 | 11% |
| 20 (default) | 20 | 7.8% | 46 | 18% |
| 35 (UI 4) | 35 | 14% | 81 | 32% |
| 100 (UI 5/max) | 100 | **39%** | 230 | **90%** |

Consistent ~2.3× everywhere. A real Tidbyt is a deliberately gentle ambient
display (≈39% duty ceiling); tronbyt currently pushes the panel to ≈90%.

### Why it feels like "orders of magnitude" at the dim end

In raw PWM it's 2.3×. But perceived brightness at low duty is roughly
logarithmic, so at the bottom of the range a 2.3× duty bump is the difference
between "barely glowing" (real Tidbyt night, ~1.2%) and "nightlight that lights
the room" (~2.7%). Subjectively that's a huge gap even though the multiplier is
modest.

### Footgun: missing header → full brightness

`remote.c` initializes `.brightness = -1` on a `uint8_t` field
(`remote.c:23`, `:184`), which is `255`. If a response has **no**
`Tronbyt-Brightness` header, `display_set_brightness(255)` clamps `pct→100` and
sets ~90% duty. So a server that *stops* sending brightness defaults to near-max,
not to `DISPLAY_DEFAULT_BRIGHTNESS` (30).

---

## 2. Night mode and dim mode

**The firmware has no night/dim concept** — grep for `night`/`gamma`/`dim` in
`main/` returns nothing. All scheduling lives on the server:

- `NightModeEnabled`, `NightStart`/`NightEnd` (HH:MM), `NightBrightness`
  (default **0**), plus overrides (`models.go:579-592`).
- `GetEffectiveBrightness()` swaps in `NightBrightness`/`DimBrightness`, so the
  firmware only ever sees the final resolved number — and runs it through the
  same `×2.3`. If `NightBrightness` is a low non-zero value, it comes out ~2.3×
  too bright. If it's `0`, the device goes dark and the server returns a default
  image (`rotation.go:55`), which works correctly.

---

## 2b. The two dimming levers (read this before chasing brightness bugs)

There are **two completely separate** ways the stack can dim the display. They
behave differently, and confusing them wastes hours.

| | **Panel brightness** | **Color filter** |
|---|---|---|
| What | global panel PWM (`setBrightness8`) | recolors the **pixels** of the rendered image |
| Where set | `Tronbyt-Brightness` header (device / night / dim brightness) | per-device `ColorFilter` / `NightColorFilter` / `DimColorFilter` |
| Applied | **instant** — firmware acts on the next fetch | **at render time** — baked into the WebP when it's generated |
| Scope | everything on the panel | the rendered image only |
| Granularity | coarse; mushy/indistinct in the bottom ~1–10/255 | fine, gamma-correct (true grey text) |

This is almost certainly how the **real Tidbyt** dimmed: its HDK firmware sets
panel brightness once at boot and never reads a brightness header, so dynamic
dimming/night mode had to be **pixel-based in the cloud render** (white → grey).

### What the color filters actually do (pixel math)

From `tronbyt/pixlet`'s `encode/filter.go` — each filter is a 3×3 matrix applied
per pixel, clamped to 0–255:

| Filter | Effect on white (255,255,255) | Character |
|---|---|---|
| `none` | unchanged (returns `nil`) | true no-op |
| **`dimmed`** | ×0.25 → **(64,64,64)** | strong, unmissable darkening |
| `moonlight` | clamps back to ~white | blue-gray tint on midtones; **barely touches white** |
| `warm` | ≈(255,255,242) | very subtle warmth |
| `redshift` | (255,227,90) | warm amber, kills blue (eye comfort, not dimming) |

So `dimmed` is a 75% cut — **impossible to miss when applied.** If you set
`dimmed` and see *nothing*, it is not being applied (see caveats below). If you
used `moonlight`/`warm` on white text, "nothing" is *expected* — they clamp white.

### Why a color filter "does nothing" — the caveats

Filters are injected only inside `renderer.Render()`. Several paths skip it
entirely (`tronbyt-server: internal/server/render_utils.go`):

1. **Not instant.** The filter is baked in on the app's **next render**
   (`UInterval` since its `LastRender`, ~line 145). Toggling a filter does **not**
   force a re-render — the cached pre-filter WebP keeps serving until then. Apps
   roll over to the dimmed look one at a time, not all at once. Force a re-render
   to test immediately.
2. **Pushed / pre-rendered apps bypass it** (`if app.Pushed { return }`, ~96–102).
3. **Static `.webp` apps bypass it** (`return content, nil, nil`, ~70–77).
   → For pushed/static content, **panel brightness is the ONLY night lever** — the
   color filter can never touch those pixels.
4. **App filter overrides device filter**; an app set to `none` suppresses the
   device night/dim filter for that app (~274–283).
5. **`NightColorFilter` only applies when night mode is actually active**
   (schedule/override); otherwise the base `ColorFilter` is used.

### The real-Tidbyt night model (recommended)

- **Color filter** (`NightColorFilter = dimmed`/`moonlight`) for the smooth,
  grey-text night dimming on **server-rendered Starlark apps**.
- **Panel brightness** (now Tidbyt-accurate 1:1, §5) as the global floor and the
  *only* lever that affects pushed/static content.

---

## 3. Architecture: board type is a BUILD-TIME choice (not flash-time / not server)

This is the key correction to the "insert a definition saying *I'm a Gen 1*"
idea — **that mechanism already exists, and it's simpler than the runtime path
you were picturing.**

There are two completely separate config tiers:

| What | Where it's set | Examples | Storage |
|------|----------------|----------|---------|
| **Which device on the server** (runtime) | "Add device" screen → flashed into the device | name, server URL, WiFi SSID/pass, API key | NVS / `secrets.json` |
| **What hardware this is** (build-time) | The binary you build | Gen1 vs Gen2 vs S3, pin maps, panel size | compiled into firmware |

Board type is a Kconfig **choice**, defaulting to Gen1:

```kconfig
# main/Kconfig.projbuild:77-103
choice BOARD_TYPE
    prompt "Board Type"
    default BOARD_TIDBYT_GEN1
    config BOARD_TIDBYT_GEN1   bool "Tidbyt Gen 1"
    config BOARD_TIDBYT_GEN2   bool "Tidbyt Gen 2"
    config BOARD_TRONBYT_S3    bool "Tronbyt S3"
    config BOARD_TRONBYT_S3_WIDE bool "Tronbyt S3 Wide"
    config BOARD_PIXOTICKER    bool "Pixoticker"
    config BOARD_MATRIXPORTAL_S3 bool "MatrixPortal S3"
    config BOARD_WAVESHARE_S3  bool "Waveshare ESP32-S3-RGB-Matrix"
endchoice
```

You pick it via the per-board defaults file at build time:

```bash
make tidbyt-gen1     # → CONFIG_BOARD_TIDBYT_GEN1=y
make tronbyt-s3      # → CONFIG_BOARD_TRONBYT_S3=y  (+ esp32s3 target, PSRAM, etc.)
```

`display.cpp` **already** branches on `CONFIG_BOARD_*` — the entire pin/size map
is a giant `#if CONFIG_BOARD_TIDBYT_GEN2 / #elif CONFIG_BOARD_TRONBYT_S3 / …`
block (`display.cpp:7-160`). So:

> ✅ You do **not** need to add anything to the server or the add-device flow.
> The Gen1 binary already *knows* it's a Gen1. We just add per-board brightness
> math to the same `#if` structure that already sets the pins.

---

## 4. Where the S3 fits in this world

- The S3 boards share the *same* `display.cpp` brightness path, so **today the
  S3 also gets the `×2.3` inflation.**
- But the S3 is **not** a real Tidbyt — it's a third-party HUB75 panel. There is
  **no Tidbyt reference** for what it "should" look like. The HDK 1:1 convention
  is the correct target for **Gen1/Gen2 only** (the actual Tidbyt hardware).
- Therefore the right model is **per-board curves**:
  - **Gen1 / Gen2** → match the real Tidbyt (1:1, ≈39% ceiling). Tidbyt-accurate.
  - **S3 / Pixoticker / MatrixPortal / Waveshare** → tune empirically to taste;
    the current `×2.3` may be fine, or you may want a different ceiling per panel.

This is exactly the design you intuited — it's just expressed at compile time.

---

## 5. Implementation (per-board scale)

There was already a (**dead**) hook for this: the old `display.cpp` referenced
`MAX_BRIGHTNESS_8BIT`, but **no board defined it anywhere**, so that clamp never
ran. It has been removed in favor of a real per-board ceiling, `BRIGHTNESS_8BIT_MAX`.

**Step 1 — the conversion ceiling is a per-board macro** with a legacy fallback
(`display.cpp`, just above `brightness_percent_to_8bit`):

```c
#ifndef BRIGHTNESS_8BIT_MAX
#define BRIGHTNESS_8BIT_MAX 230   // fallback = legacy behavior for untuned boards
#endif

static inline uint8_t brightness_percent_to_8bit(uint8_t pct) {
    if (pct > 100) pct = 100;
    return (uint8_t)(((uint32_t)pct * BRIGHTNESS_8BIT_MAX + 50) / 100);
}
```

**Step 2 — the ceiling is set inside the existing board `#if` block** (alongside
the pin/`WIDTH`/`HEIGHT` defines), for the two genuine-Tidbyt boards:

```c
// Tidbyt Gen 2 section AND Gen 1 section ("#else // GEN1 from here down"):
// Genuine Tidbyt hardware: match the stock HDK brightness convention
// (0-100% feeds setBrightness8() 1:1, ~39% max panel PWM duty).
#define BRIGHTNESS_8BIT_MAX 100
```

The S3 / Pixoticker / MatrixPortal / Waveshare sections define nothing, so they
hit the `#ifndef` fallback (230) and keep today's behavior until tuned.

With `BRIGHTNESS_8BIT_MAX = 100`, the math collapses to a true 1:1
(`(pct*100+50)/100 == pct`), so a Gen1/Gen2 behaves exactly like a real Tidbyt /
HDK. Verified mapping (standalone build of the exact conversion):

| Server % | OLD (×230) | NEW Gen1/Gen2 (×100) |
|---------:|-----------:|---------------------:|
| 3 (Dim)  | 7/255 = 2.7%  | 3/255 = 1.2% |
| 5 (Low)  | 12/255 = 4.7% | 5/255 = 2.0% |
| 12 (Med) | 28/255 = 11%  | 12/255 = 4.7% |
| 35 (High)| 81/255 = 32%  | 35/255 = 14% |
| 100 (Max)| 230/255 = 90% | 100/255 = 39% |

> Alternative considered: expose it as a Kconfig `int` per board instead of a
> `#define`. More config surface than needed — the `#define`-in-board-block
> approach matches how every other per-board hardware constant is already handled.

Still open: what the **missing-header default** should be (`remote.c:184`) —
`-1`→`255`→~max is probably not the intended fallback. Left unchanged for now.

---

## 6. How to measure / tune

The firmware already logs both numbers on every change (`display.cpp:224`):

```
I (12345) display: Setting brightness to 30% (69)
                                        ^^^     ^^
                                   server 0-100  8-bit sent to panel
```

Workflow:
1. `idf.py monitor` while the server issues normal vs. dim vs. night.
2. Confirm the first number is the raw 0–100 you expect, and watch the 8-bit
   value change with `BRIGHTNESS_8BIT_MAX`.
3. For Gen1, A/B against a real Tidbyt at the same UI level to confirm parity.
4. For S3, pick the ceiling that looks right in the room — there's no external
   reference to match.

---

## 7. Decisions & still-open items

- **Gen1/Gen2 ceiling:** ✅ **decided — `100`** (true Tidbyt parity, ≈39% duty).
  This is a deliberate, noticeable drop from today's ≈90% max; if it reads as too
  dim on real hardware, bump the `#define` in the board block (single line).
- **S3 ceiling:** keep `230` (the `#ifndef` fallback) for now; choose a tuned
  value once the panel is in hand and A/B'd in the room.
- **Optional perceptual curve:** if you'd rather the *steps* feel even (rather
  than matching raw duty), add a gamma curve in `brightness_percent_to_8bit`
  instead of a linear scale. Per-board gamma is also possible.
- **Missing-header fallback:** decide intended default brightness when the
  server omits the header (currently ~max via the `-1`/`uint8_t` quirk).

---

## Reference: file map

| Concern | File | Lines |
|---|---|---|
| Firmware percent→8-bit conversion | `main/display.cpp` | ~223–226 |
| `BRIGHTNESS_8BIT_MAX` legacy fallback (`#ifndef`) | `main/display.cpp` | ~219–221 |
| Per-board `BRIGHTNESS_8BIT_MAX 100` (Gen2 / Gen1) | `main/display.cpp` | 27, 138 |
| Per-board pin/size `#if` block | `main/display.cpp` | 7–168 |
| `setBrightness8` call | `main/display.cpp` | ~234 |
| HTTP `Tronbyt-Brightness` parse | `main/remote.c` | 73–76, 270 |
| Missing-header default (`-1`→255) | `main/remote.c` | 23, 184 |
| WS/JSON brightness parse | `main/main.c` | 206–214 |
| Board choice (Kconfig) | `main/Kconfig.projbuild` | 77–103 |
| Per-board build defaults | `sdkconfig.defaults.<board>` | — |
| Server effective brightness | `tronbyt-server: internal/data/models.go` | 922 |
| Server brightness header emit | `tronbyt-server: internal/server/handlers_device_api.go` | 139–141 |
| Server UI level → % ladder | `tronbyt-server: internal/data/models.go` | 313–320 |
| Server night/dim fields | `tronbyt-server: internal/data/models.go` | 579–592 |
| Server color-filter selection | `tronbyt-server: internal/server/render_utils.go` | 254–285 |
| Filter applied only at render | `tronbyt-server: internal/server/render_utils.go` | 79–92 |
| Pushed/static apps bypass filter | `tronbyt-server: internal/server/render_utils.go` | 70–77, 96–102 |
| Re-render interval (`UInterval`) gate | `tronbyt-server: internal/server/render_utils.go` | ~145 |
| Color-filter enum (filter names) | `tronbyt-server: internal/data/models.go` | 348–365 |
| Color-filter pixel math (3×3 matrices) | `tronbyt/pixlet: encode/filter.go` | — |

> Reference firmware (Tidbyt HDK): `setBrightness8(DISPLAY_DEFAULT_BRIGHTNESS)`
> with `DISPLAY_DEFAULT_BRIGHTNESS=30`, `MAX=100` — value passed straight through,
> no scaling, no night mode, no server-driven brightness. The HDK is the
> convention reference for Gen1/Gen2 only.
