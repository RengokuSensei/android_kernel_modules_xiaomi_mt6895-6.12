// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal cooling device stub driver for MediaTek Android thermal HAL.
 * Registers the 8 cooling devices expected by /vendor/etc/thermal_info_config.json
 * to prevent android.hardware.thermal-service.mediatek from crashing and triggering
 * the 50-second rescue reboot loop.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/thermal.h>
#include <linux/slab.h>

#define STUB_MAX_STATE 10

struct stub_cdev {
	const char *name;
	unsigned long cur_state;
	struct thermal_cooling_device *cdev;
};

static struct stub_cdev stubs[] = {
	{ .name = "thermal-cpufreq-0", .cur_state = 0 },
	{ .name = "thermal-cpufreq-1", .cur_state = 0 },
	{ .name = "thermal-devfreq-0", .cur_state = 0 },
	{ .name = "scg-off-pa2",      .cur_state = 0 },
	{ .name = "mutt-pa1",         .cur_state = 0 },
	{ .name = "mutt-pa2",         .cur_state = 0 },
	{ .name = "tx-pwr-pa1",       .cur_state = 0 },
	{ .name = "tx-pwr-pa2",       .cur_state = 0 },
};

static int stub_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	*state = STUB_MAX_STATE;
	return 0;
}

static int stub_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct stub_cdev *s = cdev->devdata;
	*state = s ? s->cur_state : 0;
	return 0;
}

static int stub_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct stub_cdev *s = cdev->devdata;
	if (s)
		s->cur_state = min_t(unsigned long, state, (unsigned long)STUB_MAX_STATE);
	return 0;
}

static const struct thermal_cooling_device_ops stub_ops = {
	.get_max_state = stub_get_max_state,
	.get_cur_state = stub_get_cur_state,
	.set_cur_state = stub_set_cur_state,
};

static int __init mtk_cooling_stub_init(void)
{
	int i;
	pr_info("mtk_cooling_stub: registering %zu cooling devices\n", ARRAY_SIZE(stubs));

	for (i = 0; i < ARRAY_SIZE(stubs); i++) {
		stubs[i].cdev = thermal_cooling_device_register(
			stubs[i].name,
			&stubs[i],
			&stub_ops);
		if (IS_ERR(stubs[i].cdev)) {
			pr_err("mtk_cooling_stub: failed to register %s: %ld\n",
			       stubs[i].name, PTR_ERR(stubs[i].cdev));
			stubs[i].cdev = NULL;
		} else {
			pr_info("mtk_cooling_stub: registered %s successfully\n", stubs[i].name);
		}
	}
	return 0;
}

static void __exit mtk_cooling_stub_exit(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(stubs); i++) {
		if (stubs[i].cdev) {
			thermal_cooling_device_unregister(stubs[i].cdev);
			stubs[i].cdev = NULL;
		}
	}
	pr_info("mtk_cooling_stub: unregistered cooling devices\n");
}

module_init(mtk_cooling_stub_init);
module_exit(mtk_cooling_stub_exit);

MODULE_AUTHOR("Antigravity Pair Programming");
MODULE_DESCRIPTION("MediaTek Android Thermal HAL Cooling Device Stubs");
MODULE_LICENSE("GPL");
