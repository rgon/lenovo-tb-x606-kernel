/*
 *  hall_switch.c - Linux kernel module for
 * 	hall_switch
 *
 *  Copyright (c) 2018
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
static struct dev_data{
		int irq_pin;
		int irq;
		struct input_dev * input_dev;
		struct device_node * dev_node;
} *hall_dev_data;

irqreturn_t hall_irq_handler(int irq, void *dev_id)
{
		struct input_dev *input_dev = (struct input_dev *)dev_id;
		int state;

		printk(KERN_INFO " hall_switch hall_irq_handler \n ");

		state = gpio_get_value(hall_dev_data->irq_pin);
		input_report_key(input_dev, KEY_F5, !state);
		input_sync(input_dev);
		return IRQ_HANDLED;
}

static int hall_switch_probe (struct platform_device *pdev)
{
		int hall_irq=0;
		int ret = -1;

		printk(KERN_INFO " hall_switch probe \n ");

		hall_dev_data = (struct dev_data *)kmalloc(sizeof(struct dev_data), GFP_KERNEL);
		if(NULL == hall_dev_data)
			return ENOMEM;
		hall_dev_data->dev_node = pdev->dev.of_node;

		hall_dev_data->irq_pin = of_get_named_gpio(hall_dev_data->dev_node, "int-gpios", 0);
		printk(KERN_INFO" we have get the gpio number gpio = %d\n", hall_dev_data->irq_pin);
		if (!gpio_is_valid(hall_dev_data->irq_pin)){
				printk(KERN_ERR"invalid gpio : %d\n", hall_dev_data->irq_pin);
				goto hall_switch_err0;
		}

		ret = gpio_request(hall_dev_data->irq_pin, "hall_irq_pin");
		if (ret != 0) {
				printk(KERN_ERR"request hall_switch gpio : %d failed \n", hall_dev_data->irq_pin);
				goto hall_switch_err0;
		}

		hall_dev_data->input_dev = input_allocate_device();
		if (!hall_dev_data->input_dev) {
				printk(KERN_ERR "failed to allocate input device for hall switch \n");
				goto hall_switch_err1;
		}
		hall_dev_data->input_dev->name  = "hall_input";
		hall_dev_data->input_dev->dev.parent = &pdev->dev;
		__set_bit(EV_KEY,hall_dev_data->input_dev->evbit);
		__set_bit(KEY_F5, hall_dev_data->input_dev->keybit);
		ret = input_register_device(hall_dev_data->input_dev);
		if(ret != 0){
				printk(KERN_ERR "hall switch input device register failed! \n");
				goto hall_switch_err2;
		}

		hall_dev_data->irq = gpio_to_irq(hall_dev_data->irq_pin);
		printk(KERN_INFO "hall switch get the irq number irq = %d\n", hall_irq);
		ret= request_irq(hall_dev_data->irq, hall_irq_handler,  IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "hall_irq", hall_dev_data->input_dev);
		if(ret != 0){
				printk(KERN_ERR "hall switch request irq failed! \n");
				goto hall_switch_err3;
		}

		return 0;


hall_switch_err3:
		input_unregister_device(hall_dev_data->input_dev);
hall_switch_err2:
		input_free_device(hall_dev_data->input_dev);
hall_switch_err1:
		gpio_free(hall_dev_data->irq_pin);
hall_switch_err0:
		kfree(hall_dev_data);

		return ret;
}

static int hall_switch_remove (struct platform_device *pdev)
{
		free_irq(hall_dev_data->irq,NULL);
		input_unregister_device(hall_dev_data->input_dev);
		input_free_device(hall_dev_data->input_dev);
		gpio_free(hall_dev_data->irq_pin);
		kfree(hall_dev_data);
		return 0;
}

static const struct of_device_id of_hall_switch_match[] = {
		{ .compatible = "hall,switch" },
		{ /* Sentinel */ }
};

static struct platform_driver hall_switch_driver =
{
		.probe = hall_switch_probe,
		.remove = hall_switch_remove,
		.driver = {
				.name   = "hall_switch",
				.owner  = THIS_MODULE,
				.of_match_table = of_hall_switch_match,
		},
};

static int  __init  hall_switch_init(void)
{
		printk(KERN_INFO " hall_switch enter \n ");
		platform_driver_register(&hall_switch_driver);
		return 0;
}

static void  __exit  hall_switch_exit(void)
{
		printk(KERN_INFO " hall_switch exit \n ");
		platform_driver_unregister(&hall_switch_driver);
}

module_init(hall_switch_init);
module_exit(hall_switch_exit);
MODULE_LICENSE("GPL");
