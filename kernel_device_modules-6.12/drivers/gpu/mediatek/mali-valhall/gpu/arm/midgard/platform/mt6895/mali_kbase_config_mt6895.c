// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include "mali_kbase_config_platform.h"
#include "platform/mtk_platform_common.h"
#include <ged_dvfs.h>
#include <mtk_gpufreq.h>
#include <mtk_gpu_utility.h>
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
#include <mboot_params.h>
#endif
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
#include <mtk_gpu_power_sspm_ipi.h>
#endif

#ifndef POWER_ON
#define POWER_ON GPU_PWR_ON
#endif
#ifndef POWER_OFF
#define POWER_OFF GPU_PWR_OFF
#endif

static inline void gpufreq_set_timestamp(void)
{
	/* Valhall CSF on MT6895 handles timestamps via internal hardware CSF timers */
}

static inline void gpufreq_check_bus_idle(void)
{
	/* Bus idle checking on MT6895 is handled by MTCMOS/SPM inside gpufreq_power_control */
}

DEFINE_MUTEX(g_mfg_lock);
static int g_cur_opp_idx;
static bool mfg_qchannel_enable;
module_param(mfg_qchannel_enable, bool, 0644);
MODULE_PARM_DESC(mfg_qchannel_enable, "Enable MFG_ACTIVE_SEL bit0 in MFG_QCHANNEL_CON (0x13FBF0B4)");

static bool mali_enable_pdca = true;
module_param(mali_enable_pdca, bool, 0644);
MODULE_PARM_DESC(mali_enable_pdca, "Enable MediaTek PDCv2 active power control in MFG_TOP_CONFIG (0x13FBF000)");

static void mt6895_pdca_config(struct kbase_device *kbdev, bool power_on)
{
	void __iomem *mfg_top;
	int i;

	if (!mali_enable_pdca) {
		dev_info(kbdev->dev, "[CSF PDC] PDCv2 config skipped (mali_enable_pdca=0)\n");
		return;
	}

	mfg_top = ioremap(0x13fbf000, 0x1000);
	if (!mfg_top) {
		dev_err(kbdev->dev, "[CSF PDC] Failed to ioremap MFG_TOP_CONFIG (0x13fbf000)\n");
		return;
	}

	if (power_on) {
		/* Step 1: Read-before-write logging (physical stacks 0, 1, 4, 5 on MT6895) */
		dev_info(kbdev->dev,
			"[CSF PDC BEFORE] CG=0x%08x ST0=0x%08x ST1=0x%08x ST4=0x%08x ST5=0x%08x\n",
			readl(mfg_top + 0x100), readl(mfg_top + 0x120), readl(mfg_top + 0x140),
			readl(mfg_top + 0xC0),  readl(mfg_top + 0x98));

		dev_info(kbdev->dev,
			"[CSF PDC BEFORE SC0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x400), readl(mfg_top + 0x418), readl(mfg_top + 0x430),
			readl(mfg_top + 0x448), readl(mfg_top + 0x460), readl(mfg_top + 0x478));

		dev_info(kbdev->dev,
			"[CSF PDC BEFORE RSV0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x404), readl(mfg_top + 0x41C), readl(mfg_top + 0x434),
			readl(mfg_top + 0x44C), readl(mfg_top + 0x464), readl(mfg_top + 0x47C));

		/* Step 2: Vendor sequence for PDCv2 power-on (__gpufreq_pdca_config) */
		/* Shader cores 0..5 active_pwrctl_en (bit 0) - Mali-G610 MC6 has 6 cores */
		for (i = 0; i < 6; i++) {
			u32 ofs = 0x400 + i * 0x18;
			writel(readl(mfg_top + ofs) | BIT(0), mfg_top + ofs);
		}

		/* Clock gating active_pwrctl_en (bit 0) */
		writel(readl(mfg_top + 0x100) | BIT(0), mfg_top + 0x100);

		/* Physical shader stacks st0, st1, st4, st5 active_pwrctl_en (bit 0) */
		/* STACK_PRESENT = 0x33: stacks 0, 1, 4, 5 exist in silicon (st2/st6 omitted) */
		writel(readl(mfg_top + 0x120) | BIT(0), mfg_top + 0x120);
		writel(readl(mfg_top + 0x140) | BIT(0), mfg_top + 0x140);
		writel(readl(mfg_top + 0xC0)  | BIT(0), mfg_top + 0xC0);
		writel(readl(mfg_top + 0x98)  | BIT(0), mfg_top + 0x98);

		/* Shader cores 0..5 active_pwrctl_rsv (bit 31) */
		for (i = 0; i < 6; i++) {
			u32 ofs = 0x404 + i * 0x18;
			writel(readl(mfg_top + ofs) | BIT(31), mfg_top + ofs);
		}

		/* Step 3: Read-after-write logging */
		dev_info(kbdev->dev,
			"[CSF PDC AFTER] CG=0x%08x ST0=0x%08x ST1=0x%08x ST4=0x%08x ST5=0x%08x\n",
			readl(mfg_top + 0x100), readl(mfg_top + 0x120), readl(mfg_top + 0x140),
			readl(mfg_top + 0xC0),  readl(mfg_top + 0x98));

		dev_info(kbdev->dev,
			"[CSF PDC AFTER SC0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x400), readl(mfg_top + 0x418), readl(mfg_top + 0x430),
			readl(mfg_top + 0x448), readl(mfg_top + 0x460), readl(mfg_top + 0x478));

		dev_info(kbdev->dev,
			"[CSF PDC AFTER RSV0..5] 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			readl(mfg_top + 0x404), readl(mfg_top + 0x41C), readl(mfg_top + 0x434),
			readl(mfg_top + 0x44C), readl(mfg_top + 0x464), readl(mfg_top + 0x47C));
	} else {
		/* Run 75: Do not clear PDCv2 active_pwrctl on power off.
		 * Clearing it destabilizes subsequent power-on cycles causing L2 timeouts.
		 * MTCMOS hardware manages core gating automatically while active.
		 */
		dev_info(kbdev->dev, "[CSF PDC] Preserving PDCv2 active_pwrctl on power off\n");
	}

	iounmap(mfg_top);
}

enum gpu_dvfs_status_step {
	GPU_DVFS_STATUS_STEP_1 = 0x1,
	GPU_DVFS_STATUS_STEP_2 = 0x2,
	GPU_DVFS_STATUS_STEP_3 = 0x3,
	GPU_DVFS_STATUS_STEP_4 = 0x4,
	GPU_DVFS_STATUS_STEP_5 = 0x5,
	GPU_DVFS_STATUS_STEP_6 = 0x6,
	GPU_DVFS_STATUS_STEP_7 = 0x7,
	GPU_DVFS_STATUS_STEP_8 = 0x8,
	GPU_DVFS_STATUS_STEP_9 = 0x9,
	GPU_DVFS_STATUS_STEP_A = 0xA,
	GPU_DVFS_STATUS_STEP_B = 0xB,
	GPU_DVFS_STATUS_STEP_C = 0xC,
	GPU_DVFS_STATUS_STEP_D = 0xD,
	GPU_DVFS_STATUS_STEP_E = 0xE,
	GPU_DVFS_STATUS_STEP_F = 0xF,
};

static inline void gpu_dvfs_status_footprint(enum gpu_dvfs_status_step step)
{
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
	aee_rr_rec_gpu_dvfs_status(step |
				(aee_rr_curr_gpu_dvfs_status() & 0xF0));
#endif
}

static inline void gpu_dvfs_status_reset_footprint(void)
{
#if IS_ENABLED(CONFIG_MTK_AEE_IPANIC)
	aee_rr_rec_gpu_dvfs_status(0);
#endif
}

static int pm_callback_power_on_nolock(struct kbase_device *kbdev)
{
#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (mtk_common_gpufreq_bringup()) {
		mtk_common_pm_mfg_active();
		return 0;
	}
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	if (mtk_common_pm_is_mfg_active())
		return 0;

	dev_vdbg(kbdev->dev, "GPU PM Callback - Active");

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_1);

	/* on,off/ SWCG(BG3D)/ MTCMOS/ BUCK */
#if defined(CONFIG_MTK_GPUFREQ_V2)
	dev_info(kbdev->dev, "GPU PM Callback - Calling gpufreq_power_control(GPU_PWR_ON)\n");
	if (gpufreq_power_control(GPU_PWR_ON) < 0) {
		dev_info(kbdev->dev, "GPU PM Callback - Power On Failed");
		return 1;
	}
	dev_info(kbdev->dev, "GPU PM Callback - Power On Successful\n");
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	/* Check SPM power & ungate BG3D clock gate */
	{
		void __iomem *spm = ioremap(0x1c001000, 0x1000);
		if (spm) {
			u32 mfg0 = readl(spm + 0xeb8);
			u32 mfg1 = readl(spm + 0xebc);
			u32 xpu = readl(spm + 0xf3c);
			dev_info(kbdev->dev, "GPU PM: SPM PWR_CON MFG0=0x%08x MFG1=0x%08x XPU=0x%08x\n",
				mfg0, mfg1, xpu);
			iounmap(spm);
		}
		void __iomem *mfg_top = ioremap(0x13fbf000, 0x1000);
		if (mfg_top) {
			u32 sta = readl(mfg_top + 0x0);
			u32 qch_before = readl(mfg_top + 0xb4);
			if (mfg_qchannel_enable) {
				writel(qch_before | BIT(0), mfg_top + 0xb4);
			}
			u32 qch_after = readl(mfg_top + 0xb4);
			dev_info(kbdev->dev, "GPU PM: mfg_top_config sta=0x%08x (BG3D bit0 %s), QCHANNEL_CON before=0x%08x after=0x%08x (mfg_qchannel_enable=%d)\n",
				sta, (sta & 1) ? "GATED" : "UNGATED", qch_before, qch_after, mfg_qchannel_enable);
			if (sta & 1) {
				dev_info(kbdev->dev, "GPU PM: ungating BG3D via clr register\n");
				writel(BIT(0), mfg_top + 0x8); /* clr_ofs = 0x8 */
				sta = readl(mfg_top + 0x0);
				dev_info(kbdev->dev, "GPU PM: mfg_top_config sta after ungate=0x%08x\n", sta);
			}
			iounmap(mfg_top);
		}
		void __iomem *infra = ioremap(0x10001000, 0x1000);
		if (infra) {
			u32 md0_en = readl(infra + 0xca0);
			u32 md0_rdy = readl(infra + 0xcac);
			u32 emi0_en = readl(infra + 0xc60);
			u32 emi0_rdy = readl(infra + 0xc6c);
			u32 emi1_en = readl(infra + 0xc70);
			u32 emi1_rdy = readl(infra + 0xc7c);

			dev_info(kbdev->dev, "GPU PM: INFRACFG MD0_MFG1 en=0x%08x rdy=0x%08x | EMISYS0 en=0x%08x rdy=0x%08x | EMISYS1 en=0x%08x rdy=0x%08x\n",
				md0_en, md0_rdy, emi0_en, emi0_rdy, emi1_en, emi1_rdy);
			iounmap(infra);
		}
		void __iomem *mfgrpc = ioremap(0x13f91000, 0x1000);
		if (mfgrpc) {
			u32 rpc_en = readl(mfgrpc + 0x40);
			u32 rpc_rdy = readl(mfgrpc + 0x48);

			dev_info(kbdev->dev, "GPU PM: MFGRPC en=0x%08x rdy=0x%08x\n", rpc_en, rpc_rdy);
			iounmap(mfgrpc);
		}
	}

	/* Ensure Common Clock Framework enables mfgcfg_bg3d clock gate */
	{
		struct clk *bg3d_clk = devm_clk_get_optional(kbdev->dev, "mfgcfg_bg3d");
		if (!IS_ERR_OR_NULL(bg3d_clk)) {
			int clk_ret = clk_prepare_enable(bg3d_clk);
			dev_info(kbdev->dev, "GPU PM: mfgcfg_bg3d clk_prepare_enable returned %d\n", clk_ret);
		}
	}

	/* Configure MediaTek PDCv2 for Mali hardware automatic MTCMOS power control */
	mt6895_pdca_config(kbdev, true);

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_2);

#if defined(CONFIG_MTK_GPUFREQ_V2)
	gpufreq_set_timestamp();
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	/* set a flag to enable GPU DVFS */
	mtk_common_pm_mfg_active();

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_3);

	/* resume frequency */
	mtk_common_gpufreq_commit(g_cur_opp_idx);

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_4);

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	ged_dvfs_gpu_clock_switch_notify(1);
#endif

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_5);

	return 0;
}

static void pm_callback_power_off_nolock(struct kbase_device *kbdev)
{
#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (mtk_common_gpufreq_bringup())
		return;
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	if (!mtk_common_pm_is_mfg_active())
		return;

	dev_vdbg(kbdev->dev, "GPU PM Callback - Idle");

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_6);

#if IS_ENABLED(CONFIG_MALI_MIDGARD_DVFS) && IS_ENABLED(CONFIG_MALI_MTK_DVFS_POLICY)
	ged_dvfs_gpu_clock_switch_notify(0);
#endif

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_7);

	/* set a flag to disable GPU DVFS */
	mtk_common_pm_mfg_idle();

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_8);

	/* suspend frequency */
	g_cur_opp_idx = mtk_common_ged_dvfs_get_last_commit_idx();

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_9);

	/* check MFG bus if idle */
#if defined(CONFIG_MTK_GPUFREQ_V2)
	gpufreq_check_bus_idle();
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_A);

	/* on,off/ SWCG(BG3D)/ MTCMOS/ BUCK */
#if defined(CONFIG_MTK_GPUFREQ_V2)
	if (gpufreq_power_control(GPU_PWR_OFF) < 0) {
		dev_info(kbdev->dev, "GPU PM Callback - Power Off Failed");
		return;
	}
#endif /* CONFIG_MTK_GPUFREQ_V2 */

	/* Clear MediaTek PDCv2 active power control on power down */
	mt6895_pdca_config(kbdev, false);

	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_B);
}

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 0;

	mutex_lock(&g_mfg_lock);
	ret = pm_callback_power_on_nolock(kbdev);
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
	MTKGPUPower_model_resume();
#endif
	mtk_notify_gpu_power_change(1);
	mutex_unlock(&g_mfg_lock);

	return ret;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	mtk_notify_gpu_power_change(0);
#if IS_ENABLED(CONFIG_MTK_GPU_SWPM_SUPPORT)
	MTKGPUPower_model_suspend();
#endif
	pm_callback_power_off_nolock(kbdev);
	mutex_unlock(&g_mfg_lock);
}

static void pm_callback_power_suspend(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	dev_vdbg(kbdev->dev, "GPU PM Callback - Suspend");
	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_E);
	mutex_unlock(&g_mfg_lock);
}

static void pm_callback_power_resume(struct kbase_device *kbdev)
{
	mutex_lock(&g_mfg_lock);
	dev_vdbg(kbdev->dev, "GPU PM Callback - Resume");
	gpu_dvfs_status_footprint(GPU_DVFS_STATUS_STEP_F);
	mutex_unlock(&g_mfg_lock);
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
	.power_suspend_callback  = pm_callback_power_suspend,
	.power_resume_callback = pm_callback_power_resume,
};

#ifndef CONFIG_OF
static struct kbase_io_resources io_resources = {
	.job_irq_number = 68,
	.mmu_irq_number = 69,
	.gpu_irq_number = 70,
	.io_memory_region = {
	.start = 0xFC010000,
	.end = 0xFC010000 + (4096 * 4) - 1
	}
};
#endif /* CONFIG_OF */

static struct kbase_platform_config versatile_platform_config = {
#ifndef CONFIG_OF
	.io_resources = &io_resources
#endif
};

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &versatile_platform_config;
}

int mtk_platform_device_init(struct kbase_device *kbdev)
{

	if (!kbdev) {
		pr_err("@%s: kbdev is NULL\n", __func__);
		return -1;
	}

	gpu_dvfs_status_reset_footprint();
	dev_info(kbdev->dev, "GPU PM Callback - Initialize Done");

	return 0;
}

void mtk_platform_device_term(struct kbase_device *kbdev) { }
