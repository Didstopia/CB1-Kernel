/*
 * Nsiway NS2009 touchscreen controller driver
 *
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.xyz>
 *
 * Some codes are from silead.c, which is
 *   Copyright (C) 2014-2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/sunxi-gpio.h>
#include <linux/delay.h>
#include "gpio_i2c.h"

/* polling interval in ms */
#define POLL_INTERVAL	10

/* this driver uses 12-bit readout */
#define MAX_12BIT	0xfff

#define NS2009_TS_NAME	"ns2009"

#define NS2009_READ_X_LOW_POWER_12BIT	0xc0
#define NS2009_READ_Y_LOW_POWER_12BIT	0xd0
#define NS2009_READ_Z1_LOW_POWER_12BIT	0xe0
#define NS2009_READ_Z2_LOW_POWER_12BIT	0xf0

#define NS2009_DEF_X_FUZZ	32
#define NS2009_DEF_Y_FUZZ	16

#define SCREEN_X_PIXEL 480
#define SCREEN_Y_PIXEL 320

/*
 * The chip have some error in z1 value when pen is up, so the data read out
 * is sometimes not accurately 0.
 * This value is based on experiements.
 */
#define NS2009_PEN_UP_Z1_ERR	80

struct ns2009_data {
	struct device ts_dev;
	struct input_dev		*input;
	bool				pen_down;
};

static int ns2009_ts_read_data(struct ns2009_data *data, u8 cmd, u16 *val)
{
	u8 raw_data[2]={0}; 
    
    i2c_read_multi_reg(cmd,raw_data,2);

	*val = (raw_data[0] << 4) | (raw_data[1] >> 4);

	return 0;
}

static int ns2009_ts_report(struct ns2009_data *data)
{
	u16 x=0, y=0, z1=0;
	int ret=0;
    
	/*
	 * NS2009 chip supports pressure measurement, but currently it needs
	 * more investigation, so we only use z1 axis to detect pen down
	 * here.
	 */
    
	ret = ns2009_ts_read_data(data, NS2009_READ_Z1_LOW_POWER_12BIT, &z1);
    
    printk("==> ns2009 report: z1 = %d\n",z1);
    
	if (ret)
		return ret;

	if (z1 >= NS2009_PEN_UP_Z1_ERR) {
		ret = ns2009_ts_read_data(data, NS2009_READ_X_LOW_POWER_12BIT,&x);
		if (ret)
			return ret;

		ret = ns2009_ts_read_data(data, NS2009_READ_Y_LOW_POWER_12BIT,&y);
		if (ret)
			return ret;

		if (!data->pen_down) {
			input_report_key(data->input, BTN_TOUCH, 1);
			data->pen_down = true;
		}
		printk("z1 = %d, x,y = [%d %d] \n", z1, x, y);
        
         //swap y
        x = 4096 - x;   
        
       // y = 4096 - y;  
        
		input_report_abs(data->input, ABS_X, y);
		input_report_abs(data->input, ABS_Y, x);
		input_sync(data->input);
	} else if (data->pen_down) {
		input_report_key(data->input, BTN_TOUCH, 0);
		input_sync(data->input);
		data->pen_down = false;
	}
	return 0;
}

static void ns2009_ts_poll(struct input_polled_dev *dev)
{
	struct ns2009_data *data = dev->private;
	int ret;

	ret = ns2009_ts_report(data);
	if (ret)
		dev_err(&dev->input->dev, "Poll touch data failed: %d\n", ret);
}

static void ns2009_ts_config_input_dev(struct ns2009_data *data)
{
	struct input_dev *input = data->input;

	input_set_abs_params(input, ABS_X, 0, MAX_12BIT, NS2009_DEF_X_FUZZ, 0);
	input_set_abs_params(input, ABS_Y, 0, MAX_12BIT, NS2009_DEF_Y_FUZZ, 0);

	input->name = NS2009_TS_NAME;
	input->phys = "input/ts";
	input->id.bustype = BUS_I2C;
	input_set_capability(input, EV_ABS, ABS_X);
	input_set_capability(input, EV_ABS, ABS_Y);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
}

static int ns2009_ts_request_polled_input_dev(struct ns2009_data *data)
{
	struct device *dev = &data->ts_dev;
	struct input_polled_dev *polled_dev;
	int error;

	polled_dev = devm_input_allocate_polled_device(dev);
	if (!polled_dev) {
		dev_err(dev,
			"Failed to allocate polled input device\n");
		return -ENOMEM;
	}
	data->input = polled_dev->input;

	ns2009_ts_config_input_dev(data);
	polled_dev->private = data;
	polled_dev->poll = ns2009_ts_poll;
	polled_dev->poll_interval = POLL_INTERVAL;

	error = input_register_polled_device(polled_dev);
	if (error) {
		dev_err(dev, "Failed to register polled input device: %d\n",
			error);
		return error;
	}

	return 0;
}

static int __init ns2009_init(void)
{
	struct ns2009_data *data;
	struct device *dev = &data->ts_dev;
	int error;
    
    printk("\n===> ns2009 init\n\n");
    
    gpio_i2c_init();

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
    
    return 0;

	error = ns2009_ts_request_polled_input_dev(data);
	if (error)
		return error;
    
    printk("===> ns2009 init OK\n");
 
	return 0;
}

static void __exit ns2009_exit(void)
{
   printk("===> ns2009 exit\n");
}

module_init(ns2009_init);
module_exit(ns2009_exit);

MODULE_LICENSE("GPL");
