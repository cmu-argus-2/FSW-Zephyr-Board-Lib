#!/usr/bin/env bash
#
# Builds a set of sample apps for the argus board. Each one exercises a
# different part of the board definition, so a broken pin, bus, devicetree
# node or Kconfig default shows up as a failed build here rather than in
# either flight software repo.
#
# Run from inside a west workspace that has this repo as a module (CI uses
# this repo's own west.yml). Compiler warnings are treated as errors.
#
#   scripts/build-samples.sh
#
# Environment overrides:
#   BOARD       board target            (default: argus/rp2350b/m33_0)
#   BUILD_ROOT  where build dirs go     (default: <this repo>/build/samples)

set -uo pipefail

BOARD="${BOARD:-argus/rp2350b/m33_0}"
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${BUILD_ROOT:-${LIB_DIR}/build/samples}"
ZEPHYR_DIR="$(west list -f '{abspath}' zephyr)" || {
    echo "error: not in a west workspace with zephyr (run 'west update' first)" >&2
    exit 1
}

# name | app path | extra west build args | what it checks
SAMPLES=(
    "hello_world|${ZEPHYR_DIR}/samples/hello_world||board boots, USB CDC ACM console"
    "hello_world_common|${ZEPHYR_DIR}/samples/hello_world|-S argus-common|argus-common snippet applies"
    "cpp_hello_world|${ZEPHYR_DIR}/samples/cpp/hello_world|-S argus-common|C++ with full libstdc++ (F Prime)"
    "i2c_scan|${LIB_DIR}/samples/i2c_scan|-S argus-common|I2C0/I2C1, zephyr,user GPIOs"
    "led_strip|${ZEPHYR_DIR}/samples/drivers/led/led_strip||NeoPixel on PIO0, led-strip alias"
    "lora_send|${ZEPHYR_DIR}/samples/drivers/lora/send||SX1262 on SPI0, lora0 alias"
    "fs_sample|${ZEPHYR_DIR}/samples/subsys/fs/fs_sample||SD card on SPI1, FAT filesystem"
    "watchdog|${ZEPHYR_DIR}/samples/drivers/watchdog||RP2350 watchdog, watchdog0 alias"
)

mkdir -p "${BUILD_ROOT}"
failed=()

for entry in "${SAMPLES[@]}"; do
    IFS='|' read -r name app extra checks <<< "${entry}"
    log="${BUILD_ROOT}/${name}.log"

    echo "::group::${name} (${checks})"
    # shellcheck disable=SC2086  # $extra is intentionally word-split
    if west build -p always -b "${BOARD}" -d "${BUILD_ROOT}/${name}" ${extra} "${app}" \
            -- -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=y > "${log}" 2>&1; then
        grep -E "^ +(FLASH|RAM):" "${log}" || true
        echo "::endgroup::"
        echo "PASS  ${name}"
    else
        tail -n 40 "${log}"
        echo "::endgroup::"
        echo "FAIL  ${name}  (full log: ${log})"
        failed+=("${name}")
    fi
done

echo
if [ "${#failed[@]}" -ne 0 ]; then
    echo "${#failed[@]} of ${#SAMPLES[@]} samples failed: ${failed[*]}"
    exit 1
fi
echo "All ${#SAMPLES[@]} samples built for ${BOARD}."
