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

#define LCD_HIMAX_CMD_WRITE	0x00
#define LCD_HIMAX_PARAM_WRITE	0x01

#define SBUS_CLK_DELAY	1
#define CS_ENABLE 	0	/* TODO : Check if 1 is needed instead (^CS) */
#define CS_DISABLE 	1	/* TODO : Check if 0 is needed instead (^CS) */


#define sbus_start(GPIOE) \
	gpio_direction_output(GPIOE, 0); \
	udelay(SBUS_CLK_DELAY)

#define sbus_stop(GPIOE)	gpio_direction_output(GPIOE, 1)


static char hx8363a_sleep_out[1] = {0x11};
static char hx8363a_set_password[4] = {0xB9, 0xFF, 0x83, 0x63};
static char hx8363a_set_power[13] = {0xB1, 0x78, 0x34, 0x08, 0x34, 0x02, 0x13, 0x11, 0x11, 0x1D,
									0x25, 0x3F, 0x3F};
/*static char hx8363a_set_disp[5] = {0xB2, 0x33, 0x33, 0x22, 0xFF};*/
static char hx8363a_set_pixel_format[2] = {0x3A, 0x70};
static char hx8363a_set_rgb_if[2] = {0xB3, 0x01};
static char hx8363a_set_cyc[10] = {0xB4, 0x08, 0x12, 0x72, 0x12, 0x06, 0x03, 0x54, 0x03, 0x4E};
static char hx8363a_set_vcom_b6[2] = {0xB6, 0x3D};
static char hx8363a_set_panel[2] = {0xCC, 0x0B};
static char hx8363a_set_gamma[31] = {0xE0, 0x01, 0x1D, 0x22, 0x33, 0x3A, 0x3F, 0x06, 0x8A, 0x8C,
									0x8F, 0x54, 0x12, 0x16, 0x4F, 0x18, 0x01, 0x1D, 0x22, 0x33,
									0x3A, 0x3F, 0x06, 0x8A, 0x8C, 0x8F, 0x54, 0x12, 0x16, 0x4F,
									0x18};
static char hx8363a_display_on[1] = {0x29}; 
static char hx8363a_display_off[1] = {0x28}; 
/*static char hx8363a_sleep_in[1] = {0x10}; */




/*
	It sends command byte or parameter byte.
	TODO : check the delay timing.
*/
static void hx8363_write_cmd(struct hx8363_platform_data *pdata, u8 cmd, u8 type)
{
	int bit;

/*Write 0 to the control bit (command byte following)*/
	gpio_direction_output(pdata->gpio_clock, 0);
	gpio_direction_output(pdata->gpio_data, type);
	udelay(1);
	gpio_direction_output(pdata->gpio_clock, 1);
	udelay(1);

/* Write the command. MSB first. */

	for (bit = 7; bit >= 0; bit--) 
	{
		gpio_direction_output(pdata->gpio_clock, 0);
		gpio_direction_output(pdata->gpio_data, (cmd >> bit) & 1);
		udelay(1);
		gpio_direction_output(pdata->gpio_clock, 1);
		udelay(1);
	}
}


/*
	 Send the command tab.
	The 3-Pin serial data packet contains a control bit DNC and a transmission byte. If DNC is
	low, the transmission byte is command byte. If DNC is high, the transmission byte is stored to
	command register. The MSB is transmitted first. The serial interface is initialized when NCS
	is high. In this state, SCL clock pulse or SDI/SDO data have no effect. A falling edge on NCS
	enables the serial interface and indicates the start of data transmission. 

	Basically we have :
	- Sending command : 0 <C7 C6 C5 C4 C3 C2 C1 C0>
	- Sending data : 1 <D7 D6 D5 D4 D3 D2 D1 D0>

	NCS : gpio_enable
	SDO : gpio_data
	SCL : gpio_clock

*/
static void hx8363_write(struct hx8363_platform_data *pdata, char *command)
{
	//First, send command.
	//then, send all the parameters, if there are any.
	int cnt_data=0;	/* Indicates the size of command + parameters. */
	int i=0;

	cnt_data = sizeof(command);


	sbus_start(pdata->gpio_enable);
	hx8363_write_cmd(pdata, command[0], LCD_HIMAX_CMD_WRITE);
	//sbus_stop(pdata->gpio_enable);		//TODO : Check if each byte write has to be surrounded by start/stop, or the whole cmd/param.

	for(i=1; i<cnt_data; i++)	/* If there are no parameters, it will not enter the loop. */
	{
		//sbus_start(pdata->gpio_enable);
		hx8363_write_cmd(pdata, command[i], LCD_HIMAX_PARAM_WRITE);
		//sbus_stop(pdata->gpio_enable);
	}
	sbus_stop(pdata->gpio_enable);

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

	/* Set initial GPIO pin directions and value. 1 : high, 0 : low 
	Warning : CS signal on the interface is «NOT CS», contrary to the NT39016.
	 gpio_clock  : SCL/SCK
	 gpio_enable : CS
	 gpio_data   : SDA/SDI
	 */

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

	/* Reset LCD panel. */
	gpio_direction_output(pdata->gpio_reset, 0);
	udelay(50);
	gpio_direction_output(pdata->gpio_reset, 1);

	/* Init panel registers. */
	hx8363_write(pdata, hx8363a_set_password);
	hx8363_write(pdata, hx8363a_set_power);
	hx8363_write(pdata, hx8363a_set_pixel_format);
	//Missing : 0x36 : Memory Access Control. Needed ?
	//Command SET_DISP is no longer in the datasheet.
	//hx8363_write(pdata, hx8363a_set_disp);	/* TODO : Change this parameter for the good resolution. */


	hx8363_write(pdata, hx8363a_set_rgb_if);
	hx8363_write(pdata, hx8363a_set_cyc);
	hx8363_write(pdata, hx8363a_set_vcom_b6);
	hx8363_write(pdata, hx8363a_set_panel);

	//TODO : Add delay here.
	hx8363_write(pdata, hx8363a_set_gamma);

	//TODO : Add delay here.
	hx8363_write(pdata, hx8363a_sleep_out);
	//TODO : Add delay here.
	hx8363_write(pdata, hx8363a_display_on);

}

static void hx8363_panel_disable(void *panel)
{
	struct hx8363_platform_data *pdata = ((struct hx8363 *)panel)->pdata;
	/* TODO : Check the needed shutdown code. */
	hx8363_write(pdata, hx8363a_display_off);
}

struct panel_ops hx8363_panel_ops = {
	.init		= hx8363_panel_init,
	.exit		= hx8363_panel_exit,
	.enable		= hx8363_panel_enable,
	.disable	= hx8363_panel_disable,
};