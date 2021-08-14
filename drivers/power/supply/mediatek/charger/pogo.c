#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

int dock_status = -1;
int usb_status = -1;
static enum power_supply_property dock_pogo_props[] = {
	POWER_SUPPLY_PROP_charge_dock,
	POWER_SUPPLY_PROP_charge_usb,
};



static int dock_pogo_property(struct power_supply *psy,
                    enum power_supply_property psp,
                    union power_supply_propval *val)
{

    switch (psp) {
    case POWER_SUPPLY_PROP_charge_dock:
        val->intval = !gpio_get_value(dock_status);
        break;
    case POWER_SUPPLY_PROP_charge_usb:
        val->intval = !gpio_get_value(usb_status);
        break;

    default:
        return -EINVAL;
    }

    return 0;
}


static const struct power_supply_desc dock_pogo_desc = {
	.name			= "customer",
	.type			= POWER_SUPPLY_TYPE_CUST,
	.properties		= dock_pogo_props,
	.num_properties		= 2,
	.get_property		= dock_pogo_property,
	.set_property		= NULL,
	.property_is_writeable	= NULL,
};




static int dock_pogo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;

	dock_status = of_get_named_gpio(node, "dock_status", 0);
	if (dock_status < 0)
	{
		printk( "dock_status is not available\n");
		return -1;
	}

	usb_status = of_get_named_gpio(node, "usb_status", 0);
	if (usb_status < 0)
	{
		printk( "usb_status is not available\n");
		return -1;
	}

	gpio_request(dock_status,"dock_status");
	gpio_direction_input(dock_status);

	gpio_request(usb_status,"usb_status");
	gpio_direction_input(usb_status);

  	power_supply_register(&pdev->dev, &dock_pogo_desc,NULL);
    return 0;
}


static struct of_device_id dock_pogo_table[] = {
	{ .compatible = "mediatek,dock_pogo",},
	{ },
};

static struct platform_driver dock_pogo_driver = {
	.probe = dock_pogo_probe,
	.driver = {
		.name = "dock_pogo",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dock_pogo_table),
	},
};


static __init int dock_pogo_init(void)
{
	return platform_driver_register(&dock_pogo_driver);
}
static __exit void dock_pogo_exit(void)
{
	//i2c_del_driver(&mm8013_i2c_driver);
}
module_init(dock_pogo_init);
module_exit(dock_pogo_exit);
MODULE_AUTHOR("leon");
MODULE_DESCRIPTION("dock pogo driver for levono m10");
MODULE_LICENSE("GPL v2");
