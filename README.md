# FSW-Zephyr-Board-Lib

Shared Zephyr board support for the CMU Argus mainboard (RP2350B). Both flight
software projects build against this one copy:

- [FSW-Mainboard-C](https://github.com/cmu-argus-2/FSW-Mainboard-C) (C)
- the F' flight software (C++)

It is a [Zephyr module](https://docs.zephyrproject.org/latest/develop/modules.html),
so nothing here is language specific. Any Zephyr app that has this module in its
build gets the `argus` board.

## What's here

| Path | What |
|---|---|
| `west.yml` | The Zephyr version and module allowlist **both** apps build against |
| `zephyr/module.yml` | Registers `boards/`, `dts/` and `snippets/` with the build |
| `boards/cmu/argus/` | Board definition: pins, buses, radio, SD card, Kconfig defaults. One revision per mainboard version |
| `dts/bindings/` | Devicetree bindings for parts Zephyr has no binding for (MAX17205) |
| `snippets/argus-common/` | Kconfig settings both apps agreed to share |

## Using it from an app

### With west (recommended)

In the app's `west.yml`, import this repo instead of listing Zephyr directly:

```yaml
manifest:
  remotes:
    - name: argus
      url-base: https://github.com/cmu-argus-2
  projects:
    - name: FSW-Zephyr-Board-Lib
      remote: argus
      revision: main        # pin a tag (e.g. v0.1.0) once we start tagging
      import: true          # brings in the Zephyr pin and modules from our west.yml
    # F' adds its own projects here (fprime, fprime-zephyr, ...)
  self:
    path: FSW-Mainboard-C   # or the F' repo's name
```

Don't list `zephyr` in the app's own manifest: west uses the first definition
it sees, so an app that pins its own Zephyr silently overrides the shared one.

Then `west update`, and build as usual:

```bash
west build -b argus/rp2350b/m33_0 -S argus-common
```

### Without west

If a project pulls this repo in some other way (git submodule, a fixed path),
point Zephyr at it before `find_package(Zephyr)` in the app's `CMakeLists.txt`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/external/FSW-Zephyr-Board-Lib)
set(SNIPPET argus-common)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

You are then responsible for checking out the same Zephyr revision as `west.yml`.

### Don't keep a second copy of the board

An app must not also have its own `boards/cmu/argus/` (or a `BOARD_ROOT`
pointing at one). Zephyr refuses to build with
`Board(s): {'argus'}, defined multiple times`.

## Where settings go

| Setting | Where | Applies to |
|---|---|---|
| Pins, buses, peripherals, `chosen`, `aliases`, `zephyr,user` GPIOs | `boards/cmu/argus/argus.dtsi`, `argus-pinctrl.dtsi` | Both apps, always |
| Hardware Kconfig (I2C clock, regulators, USB console) | `boards/cmu/argus/Kconfig.defconfig`, `argus_rp2350b_m33_0_defconfig` | Both apps, always |
| Software settings both teams agreed on | `snippets/argus-common/argus-common.conf` | Apps that enable the `argus-common` snippet |
| Stack sizes, logging, C++ and F' options, app drivers | each app's `prj.conf` | That app only |
| App-only devicetree changes | each app's `app.overlay` (try to keep this empty) | That app only |

Rule of thumb: if it describes the board, it goes in `boards/`. If both apps
want it but it's a software choice, it goes in the snippet. Everything else
stays in the app.

App code must not hardcode pins. Use the devicetree names defined here
(`led-strip`, `lora0`, `gps-uart`, `jetson-uart`, `fuel_gauge`, the
`*-gpios` properties on `zephyr,user`, ...) so a new mainboard only changes
this repo. Renaming one of these breaks both apps, so treat them as a shared API.

## Hardware revisions

Each mainboard version is a *revision* of the `argus` board, selected with
`-b argus@<N>/rp2350b/m33_0`. The base files in `boards/cmu/argus/` describe
revision 4 (pins from the CircuitPython `pins.c` for Mainboard v4).

To add a new version, e.g. 5:

1. Add `- name: "5"` under `revisions:` in `boards/cmu/argus/board.yml`.
2. Create `boards/cmu/argus/argus_rp2350b_m33_0_5.overlay` with only what changed
   from revision 4, for example a moved pin:
   ```dts
   &lora {
       busy-gpios = <&gpio0_map 26 GPIO_ACTIVE_HIGH>;
   };
   ```
   Kconfig differences go in `argus_rp2350b_m33_0_5_defconfig`.
3. Build with `-b argus@5/rp2350b/m33_0`. Once v5 is the main hardware, change
   `default:` in `board.yml`.

If a version changes the chip itself (e.g. RP2040 to RP2350), it can't be a
revision; add the new SoC under `socs:` in `board.yml` instead.

Building for a revision that isn't listed fails on purpose (`exact: true`),
so a v5 build can never silently use v4 pins.

## Board notes (revision 4)

- `PERIPH_PWR_EN` (GPIO42) must be driven high before anything on I2C0/I2C1 responds. The `periph_3v3` regulator does this at boot.
- `WDT_EN` (GPIO15) arms the external watchdog; once armed, `WDT_WDI` (GPIO2) must be toggled or the board resets. Nothing drives it by default.
- Polarity of the `*_FLT` and `BATT_ALRT` inputs has not been checked against the schematic.
- `rsense-micro-ohms` on the MAX17205 node still needs checking against the schematic.
- The MAX17205 *driver* currently lives in FSW-Mainboard-C (`src/drivers/max17205.c`). It implements Zephyr's standard `fuel_gauge` API, so it could move here for both apps to use.

## Changing this repo

Both teams flash the same board from these files, so:

1. Open a PR; get a review from someone on **each** team.
2. CI must pass, and both apps should still build for `argus/rp2350b/m33_0`.
3. Once we tag releases, each app moves to the new tag in its own `west.yml`
   when it's ready.

### CI

Every PR and push to `main` runs `.github/workflows/build.yml`. It sets up a
west workspace from this repo's `west.yml`, installs the Zephyr SDK version
Zephyr asks for, and runs `scripts/build-samples.sh`, which builds these
samples for `argus/rp2350b/m33_0` with compiler warnings as errors:

| Sample | Checks |
|---|---|
| `hello_world` | Board boots, USB CDC ACM console |
| `hello_world` + `argus-common` | The shared snippet applies |
| `cpp/hello_world` | C++ with full libstdc++ (needed by F') |
| `samples/i2c_scan` (this repo) | I2C0/I2C1, `zephyr,user` GPIOs |
| `drivers/led/led_strip` | NeoPixel on PIO0, `led-strip` alias |
| `drivers/lora/send` | SX1262 on SPI0, `lora0` alias |
| `subsys/fs/fs_sample` | SD card on SPI1, FAT filesystem |
| `drivers/watchdog` | Watchdog, `watchdog0` alias |

CI only builds. It can't flash: GitHub's runners have no board attached. Before
tagging a release, flash `samples/i2c_scan` on real hardware and check that the
mainboard parts answer.

To run the same check locally, from inside a west workspace that includes this
repo:

```bash
scripts/build-samples.sh
```

Verified building against Zephyr `8397280` with Zephyr SDK 1.0.1: Zephyr's
C `hello_world`, its C++ `cpp/hello_world` (full libstdc++, C++17), and
FSW-Mainboard-C itself.
