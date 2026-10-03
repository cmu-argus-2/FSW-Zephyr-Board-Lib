/*
 * I2C bus scan for the Argus mainboard.
 *
 * Probes every 7-bit address on I2C0 and I2C1 and prints what answers, with
 * the name of the part expected at that address. Repeats every 5 seconds, so
 * the output can be read whenever a serial terminal is opened on the USB
 * console.
 *
 * Every device on both buses is powered from the peripheral 3.3 V rail
 * (PERIPH_PWR_EN, GPIO42). The board's periph_3v3 regulator turns it on at
 * boot; if nothing answers on either bus, check that rail first.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* 0x00-0x07 and 0x78-0x7F are reserved by the I2C specification. */
#define ADDR_FIRST 0x08
#define ADDR_LAST  0x77

#define SCAN_PERIOD_MS 5000

struct known_device {
	uint8_t addr;
	const char *name;
};

/* Parts on the -X/-Y/-Z faces only answer when those face boards are connected. */
static const struct known_device i2c0_devices[] = {
	{0x29, "deployment sensor -Y (face)"},
	{0x30, "torque coil -X (face)"},
	{0x31, "torque coil -Y (face)"},
	{0x33, "torque coil -Z (face)"},
	{0x40, "solar power monitor -X (face)"},
	{0x41, "solar power monitor -Y (face)"},
	{0x44, "light sensor -X (face)"},
	{0x45, "light sensor -Y (face)"},
	{0x46, "light sensor -Z (face)"},
	{0x60, "burn wire driver -Z (face)"},
	{0x68, "IMU"},
};

/* Parts on the +X/+Y/+Z faces and the battery board only answer when connected. */
static const struct known_device i2c1_devices[] = {
	{0x0B, "MAX17205 fuel gauge, shadow RAM (battery board)"},
	{0x29, "deployment sensor +X (face)"},
	{0x30, "torque coil +X (face)"},
	{0x31, "torque coil +Y (face)"},
	{0x33, "torque coil +Z (face)"},
	{0x36, "MAX17205 fuel gauge (battery board)"},
	{0x40, "board power monitor"},
	{0x41, "GPS power monitor"},
	{0x42, "radio power monitor"},
	{0x44, "light sensor +X (face)"},
	{0x45, "light sensor +Y (face)"},
	{0x46, "Jetson power monitor"},
	{0x48, "solar power monitor +X (face)"},
	{0x49, "solar power monitor +Z (face)"},
	{0x4A, "solar power monitor +Y (face)"},
	{0x54, "sun sensor +Z/+X (face)"},
	{0x55, "sun sensor +Z/-Y (face)"},
	{0x56, "sun sensor +Z/-X (face)"},
	{0x57, "sun sensor +Z/+Y (face)"},
	{0x68, "DS3231 RTC"},
};

struct bus {
	const char *name;
	const struct device *dev;
	const struct known_device *known;
	size_t known_count;
};

static const struct bus buses[] = {
	{"I2C0 (GPIO24 SDA / GPIO25 SCL)", DEVICE_DT_GET(DT_NODELABEL(i2c0)),
	 i2c0_devices, ARRAY_SIZE(i2c0_devices)},
	{"I2C1 (GPIO46 SDA / GPIO47 SCL)", DEVICE_DT_GET(DT_NODELABEL(i2c1)),
	 i2c1_devices, ARRAY_SIZE(i2c1_devices)},
};

static const struct gpio_dt_spec periph_pwr_flt =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), periph_pwr_flt_gpios);

static const char *known_name(const struct bus *bus, uint8_t addr)
{
	for (size_t i = 0; i < bus->known_count; i++) {
		if (bus->known[i].addr == addr) {
			return bus->known[i].name;
		}
	}
	return NULL;
}

/*
 * A one-byte read rather than the usual zero-length write: the RP2350's
 * DesignWare I2C controller cannot send an address without a data byte.
 * A device that is present ACKs its address; an empty address gets a NACK.
 */
static bool probe(const struct device *dev, uint8_t addr)
{
	uint8_t byte;

	return i2c_read(dev, &byte, 1, addr) == 0;
}

static void scan_bus(const struct bus *bus)
{
	int found = 0;

	printk("\n%s\n", bus->name);

	if (!device_is_ready(bus->dev)) {
		printk("  controller not ready, skipping\n");
		return;
	}

	for (uint8_t addr = ADDR_FIRST; addr <= ADDR_LAST; addr++) {
		if (!probe(bus->dev, addr)) {
			continue;
		}

		const char *name = known_name(bus, addr);

		printk("  0x%02X  %s\n", addr, name != NULL ? name : "(not in expected list)");
		found++;
	}

	printk("  %d device(s) found\n", found);

	/* List expected parts that did not answer, so missing ones stand out. */
	for (size_t i = 0; i < bus->known_count; i++) {
		if (!probe(bus->dev, bus->known[i].addr)) {
			printk("  missing 0x%02X  %s\n", bus->known[i].addr, bus->known[i].name);
		}
	}
}

static void print_rail_fault(void)
{
	if (!gpio_is_ready_dt(&periph_pwr_flt) ||
	    gpio_pin_configure_dt(&periph_pwr_flt, GPIO_INPUT) != 0) {
		printk("PERIPH_PWR_FLT (GPIO41): could not read\n");
		return;
	}

	/* Polarity is not yet confirmed against the schematic, so print the raw level. */
	printk("PERIPH_PWR_FLT (GPIO41) raw level: %d\n", gpio_pin_get_raw(periph_pwr_flt.port, periph_pwr_flt.pin));
}

int main(void)
{
	for (unsigned int pass = 1;; pass++) {
		printk("\n===== Argus I2C scan, pass %u =====\n", pass);
		print_rail_fault();

		for (size_t i = 0; i < ARRAY_SIZE(buses); i++) {
			scan_bus(&buses[i]);
		}

		k_msleep(SCAN_PERIOD_MS);
	}

	return 0;
}
