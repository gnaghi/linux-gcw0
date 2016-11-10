/*
 * board-gcw0.c  -  GCW Zero: JZ4770-based handheld game console
 *
 * File based on Pisces board definition.
 * Copyright (C) 2006-2008, Ingenic Semiconductor Inc.
 * Original author: <jlwei@ingenic.cn>
 *
 * GCW Zero specific changes:
 * Copyright (C) 2012, Maarten ter Huurne <maarten@treewalker.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>

#include <linux/platform_data/linkdev.h>
#include <linux/platform_data/mxc6225.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/power/gpio-charger.h>
#include <linux/power/jz4770-battery.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <video/jzpanel.h>
#include <video/panel-nt39016.h>
#include <video/panel-hx8363.h>
#include <video/platform_lcd.h>

#include <asm/mach-jz4770/gpio.h>


/* LCD panel */

static struct platform_device gcw0_lcd_device;
static struct regulator *gcw0_lcd_regulator_33v;
static struct regulator *gcw0_lcd_regulator_18v;
static void *gcw0_lcd_panel;
static bool gcw0_lcd_powered;

#ifdef CONFIG_PANEL_HX8363
static struct hx8363_platform_data gcw0_panel_pdata = {
	.gpio_reset		= JZ_GPIO_PORTE(2),
	.gpio_clock		= JZ_GPIO_PORTE(15),
	.gpio_enable	= JZ_GPIO_PORTE(16),
	.gpio_data		= JZ_GPIO_PORTE(17),
};
#define LCD_panel_ops hx8363_panel_ops

#endif

#ifdef CONFIG_PANEL_NT39016

static struct nt39016_platform_data gcw0_panel_pdata = {
	.gpio_reset		= JZ_GPIO_PORTE(2),
	.gpio_clock		= JZ_GPIO_PORTE(15),
	.gpio_enable	= JZ_GPIO_PORTE(16),
	.gpio_data		= JZ_GPIO_PORTE(17),
};

#define LCD_panel_ops nt39016_panel_ops

#endif


#define GPIO_PANEL_SOMETHING	JZ_GPIO_PORTF(0)

static int gcw0_lcd_probe(struct plat_lcd_data *pdata)
{
	struct device *dev = &gcw0_lcd_device.dev;
	int ret;

	gcw0_lcd_regulator_33v = devm_regulator_get_optional(dev, "LDO6");
	if (IS_ERR(gcw0_lcd_regulator_33v)) {
		ret = PTR_ERR(gcw0_lcd_regulator_33v);
		if (ret == -ENODEV) {
			return -EPROBE_DEFER;
		} else {
			dev_err(dev, "Regulator LD06 missing: %d\n", ret);
			return ret;
		}
	}

	gcw0_lcd_regulator_18v = devm_regulator_get_optional(dev, "LDO8");
	if (IS_ERR(gcw0_lcd_regulator_18v)) {
		ret = PTR_ERR(gcw0_lcd_regulator_18v);
		if (ret == -ENODEV) {
			return -EPROBE_DEFER;
		} else {
			dev_err(dev, "Regulator LD08 missing: %d\n", ret);
			return ret;
		}
	}

	ret = LCD_panel_ops.init(&gcw0_lcd_panel, dev, &gcw0_panel_pdata);
	if (ret)
		return ret;

	ret = devm_gpio_request(dev, GPIO_PANEL_SOMETHING, "LCD panel unknown");
	if (ret) {
		dev_err(dev,
			"Failed to request LCD panel unknown pin: %d\n", ret);
		return ret;
	}

	gpio_direction_output(GPIO_PANEL_SOMETHING, 1);

	return 0;
}

static void gcw0_lcd_set_power(struct plat_lcd_data *pdata, unsigned int power)
{
	struct device *dev = &gcw0_lcd_device.dev;

	if (power == gcw0_lcd_powered)
		return;

	if (power) {
		if (regulator_enable(gcw0_lcd_regulator_33v))
			dev_err(dev, "Failed to enable 3.3V regulator\n");
		if (regulator_enable(gcw0_lcd_regulator_18v))
			dev_err(dev, "Failed to enable 1.8V regulator\n");
		LCD_panel_ops.enable(gcw0_lcd_panel);
	} else {
		LCD_panel_ops.disable(gcw0_lcd_panel);
		if (regulator_disable(gcw0_lcd_regulator_18v))
			dev_err(dev, "Failed to disable 1.8V regulator\n");
		if (regulator_disable(gcw0_lcd_regulator_33v))
			dev_err(dev, "Failed to disable 3.3V regulator\n");
	}

	gcw0_lcd_powered = power;
}

static int jz4770_lcd_match(struct plat_lcd_data *pdata, struct fb_info *info)
{
	return 1;
}

static struct plat_lcd_data gcw0_lcd_pdata = {
	.match_fb = jz4770_lcd_match,
	.probe = gcw0_lcd_probe,
	.set_power = gcw0_lcd_set_power,
};

static struct platform_device gcw0_lcd_device = {
	.name = "platform-lcd",
	.dev = {
		.platform_data = &gcw0_lcd_pdata,
	},
};


static const char * gcw0_joystick_gpiokeys_whitelist[] = {
	"evdev",
};

static const struct linkdev_pdata_device_info gcw0_joystick_devices[] = {
	{
		.name = "analog joystick",
	},
	{
		.name = "board:gpio-keys",
		.handlers_whitelist = gcw0_joystick_gpiokeys_whitelist,
		.nb_handlers = ARRAY_SIZE(gcw0_joystick_gpiokeys_whitelist),
	},
};

static const struct linkdev_pdata_key_map gcw0_key_map[] = {
	{
		.code = KEY_UP,
		.event = {
			.type = EV_ABS,
			.code = ABS_HAT0Y,
			.value = -1,
		},
	},
	{
		.code = KEY_DOWN,
		.event = {
			.type = EV_ABS,
			.code = ABS_HAT0Y,
			.value = 1,
		}
	},
	{
		.code = KEY_LEFT,
		.event = {
			.type = EV_ABS,
			.code = ABS_HAT0X,
			.value = -1,
		},
	},
	{
		.code = KEY_RIGHT,
		.event = {
			.type = EV_ABS,
			.code = ABS_HAT0X,
			.value = 1,
		},
	},
	{
		.code = KEY_LEFTCTRL,
		.event.code = BTN_EAST,
	},
	{
		.code = KEY_LEFTALT,
		.event.code = BTN_SOUTH,
	},
	{
		.code = KEY_LEFTSHIFT,
		.event.code = BTN_WEST,
	},
	{
		.code = KEY_SPACE,
		.event.code = BTN_NORTH,
	},
	{
		.code = KEY_ENTER,
		.event.code = BTN_START,
	},
	{
		.code = KEY_ESC,
		.event.code = BTN_SELECT,
	},
	{
		.code = KEY_TAB,
		.event.code = BTN_THUMBL,
	},
	{
		.code = KEY_BACKSPACE,
		.event.code = BTN_THUMBR,
	},
};

static const struct linkdev_pdata_abs_map gcw0_abs_map[] = {
	{
		.name = "analog joystick",
		.axis = ABS_X,
		.axis_dest = ABS_X,
	},
	{
		.name = "analog joystick",
		.axis = ABS_Y,
		.axis_dest = ABS_Y,
	},
	{
		.name = "gpio-keys",
		.axis = ABS_HAT0X,
		.axis_dest = ABS_HAT0X,
	},
	{
		.name = "gpio-keys",
		.axis = ABS_HAT0Y,
		.axis_dest = ABS_HAT0Y,
	},
};

static struct linkdev_platform_data gcw0_joystick_pdata = {
	/* This specific name informs SDL about the composition of the joystick */
	.name = "linkdev device (Analog 2-axis 8-button 2-hat)",
	.devices = gcw0_joystick_devices,
	.nb_devices = ARRAY_SIZE(gcw0_joystick_devices),
	.key_map = gcw0_key_map,
	.key_map_size = ARRAY_SIZE(gcw0_key_map),
	.abs_map = gcw0_abs_map,
	.abs_map_size = ARRAY_SIZE(gcw0_abs_map),
};

/* GCW0 Input driver */
static struct platform_device gcw0_joystick_device = {
	.name = "linkdev",
	.id = -1,
	.dev = {
		.platform_data = &gcw0_joystick_pdata,
	},
};


/* Device registration */

static struct platform_device *jz_platform_devices[] __initdata = {
	&gcw0_lcd_device,
	&gcw0_joystick_device,
};

static int __init gcw0_init_platform_devices(void)
{
	return platform_add_devices(jz_platform_devices,
				    ARRAY_SIZE(jz_platform_devices));
}

static int __init gcw0_board_setup(void)
{
	printk(KERN_INFO "GCW Zero JZ4770 setup\n");

	if (gcw0_init_platform_devices())
		panic("Failed to initialize platform devices");

	return 0;
}

arch_initcall(gcw0_board_setup);
