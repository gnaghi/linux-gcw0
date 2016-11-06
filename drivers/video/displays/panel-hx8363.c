/*
 * panel-hx8363.c -- hx8363 controller driver
 *
 * Copyright (C) 2016, Gnaghi
 *
 *
 * Based on the nt39016 lcd driver:
 * Copyright (C) 2012, Maarten ter Huurne <maarten@treewalker.org>
 * Copyright (C) 2014, Paul Cercueil <paul@crapouillou.net>
 *
 * Includes code fragments from various snippets found on different Linux kernels.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/gpio.h>

#include <video/jzpanel.h>
#include <video/panel-hx8363.h>


struct hx8363 {
	struct hx8363_platform_data *pdata;
};


static char hx8363a_sleep_out[1] = {0x11};
static char hx8363a_set_password[4] = {0xB9, 0xFF, 0x83, 0x63};
static char hx8363a_set_power[13] = {0xB1, 0x78, 0x34, 0x08, 0x34, 0x02, 0x13, 0x11, 0x11, 0x1D,
									0x25, 0x3F, 0x3F};
static char hx8363a_set_disp[5] = {0xB2, 0x33, 0x33, 0x22, 0xFF};
static char hx8363a_set_mipi[14] = {0xBA, 0x80, 0x00, 0x10, 0x08, 0x08, 0x10, 0x7C, 0x6E, 0x6D,
									0x0A, 0x01, 0x84, 0x43};
static char hx8363a_set_pixel_format[2] = {0x3A, 0x70};
static char hx8363a_set_rgb_if[2] = {0xB3, 0x00};
static char hx8363a_set_cyc[10] = {0xB4, 0x08, 0x12, 0x72, 0x12, 0x06, 0x03, 0x54, 0x03, 0x4E};
static char hx8363a_set_vcom_b6[2] = {0xB6, 0x3D};
static char hx8363a_set_vcom_bf[3] = {0xBF, 0x00, 0x10};
static char hx8363a_set_panel[2] = {0xCC, 0x0B};
static char hx8363a_set_gamma[31] = {0xE0, 0x01, 0x1D, 0x22, 0x33, 0x3A, 0x3F, 0x06, 0x8A, 0x8C,
									0x8F, 0x54, 0x12, 0x16, 0x4F, 0x18, 0x01, 0x1D, 0x22, 0x33,
									0x3A, 0x3F, 0x06, 0x8A, 0x8C, 0x8F, 0x54, 0x12, 0x16, 0x4F,
									0x18};
static char hx8363a_display_on[1] = {0x29}; 
static char hx8363a_display_off[1] = {0x28}; 
static char hx8363a_sleep_in[1] = {0x10}; 


#define RV(reg, val) (((reg) << 10) | (1 << 9) | (val))
static const u16 panel_data[] = {
	RV(0x00, 0x07), RV(0x01, 0x00), RV(0x02, 0x03), RV(0x03, 0xCC),
	RV(0x04, 0x46), RV(0x05, 0x05), RV(0x06, 0x00), RV(0x07, 0x00),
	RV(0x08, 0x08), RV(0x09, 0x40), RV(0x0A, 0x88), RV(0x0B, 0x88),
	RV(0x0C, 0x20), RV(0x0D, 0x20), RV(0x0E, 0x67), RV(0x0F, 0xA4),
	RV(0x10, 0x04), RV(0x11, 0x24), RV(0x12, 0x24), RV(0x20, 0x00),
};

static const u16 panel_standby = RV(0x00, 0x05);
#undef RV

static void hx8363_write_reg(struct hx8363_platform_data *pdata, u16 data)
{
	int bit;

	udelay(2);
	gpio_direction_output(pdata->gpio_enable, 0);

	for (bit = 15; bit >= 0; bit--) {
		gpio_direction_output(pdata->gpio_clock, 0);
		gpio_direction_output(pdata->gpio_data, (data >> bit) & 1);
		udelay(1);
		gpio_direction_output(pdata->gpio_clock, 1);
		udelay(1);
	}

	gpio_direction_output(pdata->gpio_enable, 1);

	/* Note: Both clock and enable pin are left in inactive state (1). */
}

static int hx8363_panel_init(void **out_panel, struct device *dev,
			      void *panel_pdata)
{
	struct hx8363_platform_data *pdata = panel_pdata;
	struct hx8363 *panel;
	int ret;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel) {
		dev_err(dev, "Failed to alloc panel data\n");
		return -ENOMEM;
	}

	panel->pdata = pdata;

	*out_panel = panel;

	/* Reserve GPIO pins. */

	ret = devm_gpio_request(dev, pdata->gpio_reset, "LCD panel reset");
	if (ret) {
		dev_err(dev,
			"Failed to request LCD panel reset pin: %d\n", ret);
		return ret;
	}

	ret = devm_gpio_request(dev, pdata->gpio_clock, "LCD 3-wire clock");
	if (ret) {
		dev_err(dev,
			"Failed to request LCD panel 3-wire clock pin: %d\n",
			ret);
		return ret;
	}

	ret = devm_gpio_request(dev, pdata->gpio_enable, "LCD 3-wire enable");
	if (ret) {
		dev_err(dev,
			"Failed to request LCD panel 3-wire enable pin: %d\n",
			ret);
		return ret;
	}

	ret = devm_gpio_request(dev, pdata->gpio_data, "LCD 3-wire data");
	if (ret) {
		dev_err(dev,
			"Failed to request LCD panel 3-wire data pin: %d\n",
			ret);
		return ret;
	}

	/* Set initial GPIO pin directions and value. */

	gpio_direction_output(pdata->gpio_clock,  1);
	gpio_direction_output(pdata->gpio_enable, 1);
	gpio_direction_output(pdata->gpio_data,   0);

	return 0;
}

static void hx8363_panel_exit(void *panel)
{
}

static void hx8363_panel_enable(void *panel)
{
	struct hx8363_platform_data *pdata = ((struct hx8363 *)panel)->pdata;
	int i;

	/* Reset LCD panel. */
	gpio_direction_output(pdata->gpio_reset, 0);
	udelay(50);
	gpio_direction_output(pdata->gpio_reset, 1);

	/* Init panel registers. */
	for (i = 0; i < ARRAY_SIZE(panel_data); i++)
		hx8363_write_reg(pdata, panel_data[i]);
}

static void hx8363_panel_disable(void *panel)
{
	struct hx8363_platform_data *pdata = ((struct hx8363 *)panel)->pdata;

	hx8363_write_reg(pdata, panel_standby);
}

struct panel_ops hx8363_panel_ops = {
	.init		= hx8363_panel_init,
	.exit		= hx8363_panel_exit,
	.enable		= hx8363_panel_enable,
	.disable	= hx8363_panel_disable,
};