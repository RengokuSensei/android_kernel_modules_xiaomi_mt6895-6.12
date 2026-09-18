/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Goodix GF3626ZS9 Fingerprint Sensor Driver Header
 *
 * Copyright (C) 2026 Goodix, Inc.
 */

#ifndef _GF3626ZS9_H_
#define _GF3626ZS9_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define GF_IOC_MAGIC 'g'

struct gf_key_event {
	__u32 key;
	__u32 value;
};

#define GF_IOC_INIT		_IOR(GF_IOC_MAGIC, 0, __u8)
#define GF_IOC_EXIT		_IO(GF_IOC_MAGIC, 1)
#define GF_IOC_RESET		_IOW(GF_IOC_MAGIC, 2, __u32)
#define GF_IOC_ENABLE_IRQ	_IO(GF_IOC_MAGIC, 3)
#define GF_IOC_DISABLE_IRQ	_IO(GF_IOC_MAGIC, 4)
#define GF_IOC_ENABLE_SPI_CLK	_IO(GF_IOC_MAGIC, 5)
#define GF_IOC_DISABLE_SPI_CLK	_IO(GF_IOC_MAGIC, 6)
#define GF_IOC_ENABLE_POWER	_IO(GF_IOC_MAGIC, 7)
#define GF_IOC_DISABLE_POWER	_IO(GF_IOC_MAGIC, 8)
#define GF_IOC_INPUT_KEY_EVENT	_IOW(GF_IOC_MAGIC, 9, struct gf_key_event)
#define GF_IOC_ENTER_SLEEP_MODE	_IO(GF_IOC_MAGIC, 10)
#define GF_IOC_GET_FW_INFO	_IOR(GF_IOC_MAGIC, 11, __u8)
#define GF_IOC_REMOVE		_IO(GF_IOC_MAGIC, 12)

#endif /* _GF3626ZS9_H_ */
