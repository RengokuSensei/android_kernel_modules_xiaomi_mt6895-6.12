// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix GF3626ZS9 Fingerprint Sensor Driver
 *
 * Ported to standard Linux 6.12 SPI subsystem APIs.
 * Copyright (C) 2026 Goodix, Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/input.h>
#include <linux/pinctrl/consumer.h>

#include "gf3626zs9.h"

#define GF_DRIVER_NAME		"goodix_fp"
#define GF_CLASS_NAME		"goodix_fp"
#define GF_DEV_NAME		"goodix_fp"

#define GF_MAX_BUFFER_SIZE	(1024 * 32)

struct gf_dev {
	struct spi_device	*spi;
	struct cdev		cdev;
	struct class		*class;
	struct device		*device;
	dev_t			devno;

	struct input_dev	*input_dev;

	int			reset_gpio;
	int			irq_gpio;
	int			irq;
	bool			irq_enabled;

	struct regulator	*vfp;
	struct regulator	*vibr;
	struct regulator	*vufs18;
	bool			power_enabled;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pin_default;
	struct pinctrl_state	*pin_reset_high;
	struct pinctrl_state	*pin_reset_low;
	struct pinctrl_state	*pin_eint_init;
	struct pinctrl_state	*pin_spi_cs;

	struct mutex		buf_lock;
	struct mutex		frame_lock;

	u8			*spi_tx_buf;
	u8			*spi_rx_buf;
};

static struct gf_dev *g_gf_dev;

static int gf_power_on(struct gf_dev *gf_dev)
{
	int ret;

	if (gf_dev->power_enabled)
		return 0;

	if (gf_dev->vfp) {
		ret = regulator_enable(gf_dev->vfp);
		if (ret)
			dev_err(&gf_dev->spi->dev, "Failed to enable vfp: %d\n", ret);
	}

	if (gf_dev->vibr) {
		ret = regulator_enable(gf_dev->vibr);
		if (ret)
			dev_err(&gf_dev->spi->dev, "Failed to enable vibr: %d\n", ret);
	}

	if (gf_dev->vufs18) {
		ret = regulator_enable(gf_dev->vufs18);
		if (ret)
			dev_err(&gf_dev->spi->dev, "Failed to enable vufs18: %d\n", ret);
	}

	gf_dev->power_enabled = true;
	msleep(10);
	return 0;
}

static int gf_power_off(struct gf_dev *gf_dev)
{
	if (!gf_dev->power_enabled)
		return 0;

	if (gf_dev->vufs18)
		regulator_disable(gf_dev->vufs18);

	if (gf_dev->vibr)
		regulator_disable(gf_dev->vibr);

	if (gf_dev->vfp)
		regulator_disable(gf_dev->vfp);

	gf_dev->power_enabled = false;
	return 0;
}

static void gf_hw_reset(struct gf_dev *gf_dev)
{
	if (gf_dev->pin_reset_low && gf_dev->pin_reset_high) {
		pinctrl_select_state(gf_dev->pinctrl, gf_dev->pin_reset_low);
		msleep(20);
		pinctrl_select_state(gf_dev->pinctrl, gf_dev->pin_reset_high);
		msleep(20);
	} else if (gpio_is_valid(gf_dev->reset_gpio)) {
		gpio_set_value(gf_dev->reset_gpio, 0);
		msleep(20);
		gpio_set_value(gf_dev->reset_gpio, 1);
		msleep(20);
	}
}

static int gf_spi_transfer(struct gf_dev *gf_dev, const u8 *tx_buf, u8 *rx_buf, size_t len)
{
	struct spi_transfer xfer = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len = len,
		.speed_hz = gf_dev->spi->max_speed_hz,
	};
	struct spi_message msg;

	if (len > GF_MAX_BUFFER_SIZE)
		return -EINVAL;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(gf_dev->spi, &msg);
}

static irqreturn_t gf_irq_handler(int irq, void *dev_id)
{
	struct gf_dev *gf_dev = dev_id;

	if (!gf_dev)
		return IRQ_NONE;

	sysfs_notify(&gf_dev->spi->dev.kobj, NULL, "irq");

	return IRQ_HANDLED;
}

static int gf_enable_irq(struct gf_dev *gf_dev)
{
	if (!gf_dev->irq_enabled && gf_dev->irq > 0) {
		enable_irq(gf_dev->irq);
		gf_dev->irq_enabled = true;
	}
	return 0;
}

static int gf_disable_irq(struct gf_dev *gf_dev)
{
	if (gf_dev->irq_enabled && gf_dev->irq > 0) {
		disable_irq(gf_dev->irq);
		gf_dev->irq_enabled = false;
	}
	return 0;
}

static ssize_t gf_irq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gf_dev *gf_dev = dev_get_drvdata(dev);
	int val = 0;

	if (gpio_is_valid(gf_dev->irq_gpio))
		val = gpio_get_value(gf_dev->irq_gpio);

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static DEVICE_ATTR(irq, 0444, gf_irq_show, NULL);

static int gf_open(struct inode *inode, struct file *filp)
{
	struct gf_dev *gf_dev = container_of(inode->i_cdev, struct gf_dev, cdev);

	filp->private_data = gf_dev;
	return 0;
}

static int gf_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static ssize_t gf_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct gf_dev *gf_dev = filp->private_data;
	int ret;

	if (count > GF_MAX_BUFFER_SIZE)
		return -EINVAL;

	mutex_lock(&gf_dev->buf_lock);
	memset(gf_dev->spi_rx_buf, 0, count);

	ret = gf_spi_transfer(gf_dev, NULL, gf_dev->spi_rx_buf, count);
	if (ret == 0) {
		if (copy_to_user(buf, gf_dev->spi_rx_buf, count))
			ret = -EFAULT;
		else
			ret = count;
	}
	mutex_unlock(&gf_dev->buf_lock);

	return ret;
}

static ssize_t gf_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct gf_dev *gf_dev = filp->private_data;
	int ret;

	if (count > GF_MAX_BUFFER_SIZE)
		return -EINVAL;

	mutex_lock(&gf_dev->buf_lock);
	if (copy_from_user(gf_dev->spi_tx_buf, buf, count)) {
		mutex_unlock(&gf_dev->buf_lock);
		return -EFAULT;
	}

	ret = gf_spi_transfer(gf_dev, gf_dev->spi_tx_buf, NULL, count);
	if (ret == 0)
		ret = count;

	mutex_unlock(&gf_dev->buf_lock);

	return ret;
}

static long gf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gf_dev *gf_dev = filp->private_data;
	struct gf_key_event key_ev;
	int ret = 0;
	u8 info = 0;

	if (!gf_dev)
		return -ENODEV;

	switch (cmd) {
	case GF_IOC_INIT:
		gf_power_on(gf_dev);
		gf_hw_reset(gf_dev);
		info = 0x01;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	case GF_IOC_EXIT:
		gf_disable_irq(gf_dev);
		gf_power_off(gf_dev);
		break;
	case GF_IOC_RESET:
		gf_hw_reset(gf_dev);
		break;
	case GF_IOC_ENABLE_IRQ:
		gf_enable_irq(gf_dev);
		break;
	case GF_IOC_DISABLE_IRQ:
		gf_disable_irq(gf_dev);
		break;
	case GF_IOC_ENABLE_SPI_CLK:
	case GF_IOC_DISABLE_SPI_CLK:
		/* Handled by standard Linux SPI subsystem automatically */
		ret = 0;
		break;
	case GF_IOC_ENABLE_POWER:
		ret = gf_power_on(gf_dev);
		break;
	case GF_IOC_DISABLE_POWER:
		ret = gf_power_off(gf_dev);
		break;
	case GF_IOC_INPUT_KEY_EVENT:
		if (copy_from_user(&key_ev, (void __user *)arg, sizeof(key_ev))) {
			ret = -EFAULT;
			break;
		}
		if (gf_dev->input_dev) {
			input_report_key(gf_dev->input_dev, key_ev.key, key_ev.value);
			input_sync(gf_dev->input_dev);
		}
		break;
	case GF_IOC_ENTER_SLEEP_MODE:
		gf_disable_irq(gf_dev);
		break;
	case GF_IOC_GET_FW_INFO:
		info = 0x00;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	case GF_IOC_REMOVE:
		ret = 0;
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static const struct file_operations gf_fops = {
	.owner          = THIS_MODULE,
	.open           = gf_open,
	.release        = gf_release,
	.read           = gf_read,
	.write          = gf_write,
	.unlocked_ioctl = gf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = gf_ioctl,
#endif
};

static int gf_parse_dts(struct gf_dev *gf_dev)
{
	struct device *dev = &gf_dev->spi->dev;
	struct device_node *np = dev->of_node;
	int ret;

	if (!np)
		return -EINVAL;

	gf_dev->irq_gpio = of_get_named_gpio(np, "fpc,gpio_irq", 0);
	if (!gpio_is_valid(gf_dev->irq_gpio))
		gf_dev->irq_gpio = of_get_named_gpio(np, "fp-irq-gpio", 0);

	if (gpio_is_valid(gf_dev->irq_gpio)) {
		ret = gpio_request(gf_dev->irq_gpio, "gf_irq_gpio");
		if (ret) {
			dev_err(dev, "Failed to request irq gpio: %d\n", ret);
			return ret;
		}
		gpio_direction_input(gf_dev->irq_gpio);
		gf_dev->irq = gpio_to_irq(gf_dev->irq_gpio);
	} else {
		gf_dev->irq = gf_dev->spi->irq;
	}

	gf_dev->reset_gpio = of_get_named_gpio(np, "fp-reset-gpio", 0);
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		ret = gpio_request(gf_dev->reset_gpio, "gf_reset_gpio");
		if (ret) {
			dev_err(dev, "Failed to request reset gpio: %d\n", ret);
			if (gpio_is_valid(gf_dev->irq_gpio))
				gpio_free(gf_dev->irq_gpio);
			return ret;
		}
		gpio_direction_output(gf_dev->reset_gpio, 1);
	}

	gf_dev->vfp = regulator_get(dev, "vfp");
	if (IS_ERR(gf_dev->vfp))
		gf_dev->vfp = NULL;

	gf_dev->vibr = regulator_get(dev, "vibr");
	if (IS_ERR(gf_dev->vibr))
		gf_dev->vibr = NULL;

	gf_dev->vufs18 = regulator_get(dev, "vufs18");
	if (IS_ERR(gf_dev->vufs18))
		gf_dev->vufs18 = NULL;

	gf_dev->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gf_dev->pinctrl)) {
		gf_dev->pin_default = pinctrl_lookup_state(gf_dev->pinctrl, "default");
		gf_dev->pin_reset_high = pinctrl_lookup_state(gf_dev->pinctrl, "reset_high");
		gf_dev->pin_reset_low = pinctrl_lookup_state(gf_dev->pinctrl, "reset_low");
		gf_dev->pin_eint_init = pinctrl_lookup_state(gf_dev->pinctrl, "eint_init");
		gf_dev->pin_spi_cs = pinctrl_lookup_state(gf_dev->pinctrl, "spi_cs");

		if (gf_dev->pin_default)
			pinctrl_select_state(gf_dev->pinctrl, gf_dev->pin_default);
		if (gf_dev->pin_eint_init)
			pinctrl_select_state(gf_dev->pinctrl, gf_dev->pin_eint_init);
		if (gf_dev->pin_spi_cs)
			pinctrl_select_state(gf_dev->pinctrl, gf_dev->pin_spi_cs);
	}

	return 0;
}

static int gf_probe(struct spi_device *spi)
{
	struct gf_dev *gf_dev;
	int ret;

	gf_dev = kzalloc(sizeof(*gf_dev), GFP_KERNEL);
	if (!gf_dev)
		return -ENOMEM;

	gf_dev->spi = spi;
	spi_set_drvdata(spi, gf_dev);

	gf_dev->spi_tx_buf = kzalloc(GF_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!gf_dev->spi_tx_buf) {
		ret = -ENOMEM;
		goto err_free_dev;
	}

	gf_dev->spi_rx_buf = kzalloc(GF_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!gf_dev->spi_rx_buf) {
		ret = -ENOMEM;
		goto err_free_tx_buf;
	}

	mutex_init(&gf_dev->buf_lock);
	mutex_init(&gf_dev->frame_lock);

	ret = gf_parse_dts(gf_dev);
	if (ret)
		goto err_free_rx_buf;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		goto err_free_gpios;
	}

	ret = alloc_chrdev_region(&gf_dev->devno, 0, 1, GF_DEV_NAME);
	if (ret) {
		dev_err(&spi->dev, "alloc_chrdev_region failed: %d\n", ret);
		goto err_free_gpios;
	}

	cdev_init(&gf_dev->cdev, &gf_fops);
	gf_dev->cdev.owner = THIS_MODULE;
	ret = cdev_add(&gf_dev->cdev, gf_dev->devno, 1);
	if (ret) {
		dev_err(&spi->dev, "cdev_add failed: %d\n", ret);
		goto err_unregister_devno;
	}

	gf_dev->class = class_create(GF_CLASS_NAME);
	if (IS_ERR(gf_dev->class)) {
		ret = PTR_ERR(gf_dev->class);
		dev_err(&spi->dev, "class_create failed: %d\n", ret);
		goto err_del_cdev;
	}

	gf_dev->device = device_create(gf_dev->class, &spi->dev, gf_dev->devno, gf_dev, GF_DEV_NAME);
	if (IS_ERR(gf_dev->device)) {
		ret = PTR_ERR(gf_dev->device);
		dev_err(&spi->dev, "device_create failed: %d\n", ret);
		goto err_destroy_class;
	}

	gf_dev->input_dev = input_allocate_device();
	if (gf_dev->input_dev) {
		gf_dev->input_dev->name = GF_DRIVER_NAME;
		set_bit(EV_KEY, gf_dev->input_dev->evbit);
		set_bit(KEY_HOMEPAGE, gf_dev->input_dev->keybit);
		set_bit(KEY_CAMERA, gf_dev->input_dev->keybit);
		set_bit(KEY_POWER, gf_dev->input_dev->keybit);

		ret = input_register_device(gf_dev->input_dev);
		if (ret) {
			dev_err(&spi->dev, "input_register_device failed: %d\n", ret);
			input_free_device(gf_dev->input_dev);
			gf_dev->input_dev = NULL;
		}
	}

	if (gf_dev->irq > 0) {
		ret = request_threaded_irq(gf_dev->irq, NULL, gf_irq_handler,
					   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					   GF_DRIVER_NAME, gf_dev);
		if (ret) {
			dev_err(&spi->dev, "request_threaded_irq failed: %d\n", ret);
			goto err_unregister_input;
		}
		gf_dev->irq_enabled = true;
	}

	ret = device_create_file(&spi->dev, &dev_attr_irq);
	if (ret)
		dev_warn(&spi->dev, "Failed to create sysfs attribute irq: %d\n", ret);

	g_gf_dev = gf_dev;
	dev_info(&spi->dev, "GF3626ZS9 fingerprint driver probed successfully\n");
	return 0;

err_unregister_input:
	if (gf_dev->input_dev)
		input_unregister_device(gf_dev->input_dev);
	device_destroy(gf_dev->class, gf_dev->devno);
err_destroy_class:
	class_destroy(gf_dev->class);
err_del_cdev:
	cdev_del(&gf_dev->cdev);
err_unregister_devno:
	unregister_chrdev_region(gf_dev->devno, 1);
err_free_gpios:
	if (gpio_is_valid(gf_dev->reset_gpio))
		gpio_free(gf_dev->reset_gpio);
	if (gpio_is_valid(gf_dev->irq_gpio))
		gpio_free(gf_dev->irq_gpio);
	if (gf_dev->vfp)
		regulator_put(gf_dev->vfp);
	if (gf_dev->vibr)
		regulator_put(gf_dev->vibr);
	if (gf_dev->vufs18)
		regulator_put(gf_dev->vufs18);
err_free_rx_buf:
	mutex_destroy(&gf_dev->buf_lock);
	mutex_destroy(&gf_dev->frame_lock);
	kfree(gf_dev->spi_rx_buf);
err_free_tx_buf:
	kfree(gf_dev->spi_tx_buf);
err_free_dev:
	kfree(gf_dev);
	return ret;
}

static void gf_remove(struct spi_device *spi)
{
	struct gf_dev *gf_dev = spi_get_drvdata(spi);

	if (!gf_dev)
		return;

	device_remove_file(&spi->dev, &dev_attr_irq);

	if (gf_dev->irq > 0)
		free_irq(gf_dev->irq, gf_dev);

	if (gf_dev->input_dev)
		input_unregister_device(gf_dev->input_dev);

	device_destroy(gf_dev->class, gf_dev->devno);
	class_destroy(gf_dev->class);
	cdev_del(&gf_dev->cdev);
	unregister_chrdev_region(gf_dev->devno, 1);

	gf_power_off(gf_dev);

	if (gpio_is_valid(gf_dev->reset_gpio))
		gpio_free(gf_dev->reset_gpio);
	if (gpio_is_valid(gf_dev->irq_gpio))
		gpio_free(gf_dev->irq_gpio);

	if (gf_dev->vfp)
		regulator_put(gf_dev->vfp);
	if (gf_dev->vibr)
		regulator_put(gf_dev->vibr);
	if (gf_dev->vufs18)
		regulator_put(gf_dev->vufs18);

	mutex_destroy(&gf_dev->buf_lock);
	mutex_destroy(&gf_dev->frame_lock);

	kfree(gf_dev->spi_rx_buf);
	kfree(gf_dev->spi_tx_buf);
	kfree(gf_dev);

	g_gf_dev = NULL;
}

static const struct of_device_id gf_of_match[] = {
	{ .compatible = "goodix,goodix-fp" },
	{ }
};
MODULE_DEVICE_TABLE(of, gf_of_match);

static const struct spi_device_id gf_spi_id[] = {
	{ GF_DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, gf_spi_id);

static struct spi_driver gf_driver = {
	.driver = {
		.name = GF_DRIVER_NAME,
		.of_match_table = gf_of_match,
	},
	.probe = gf_probe,
	.remove = gf_remove,
	.id_table = gf_spi_id,
};

module_spi_driver(gf_driver);

MODULE_AUTHOR("Goodix");
MODULE_DESCRIPTION("Goodix GF3626ZS9 Fingerprint Sensor Driver");
MODULE_LICENSE("GPL v2");
