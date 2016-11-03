#ifndef __PANEL_HX8363_H
#define __PANEL_HX8363_H

struct hx8363_platform_data {
	int gpio_reset;
	int gpio_clock;
	int gpio_enable;
	int gpio_data;
};

extern struct panel_ops hx8363_panel_ops;

#endif /* __PANEL_HX8363_H */