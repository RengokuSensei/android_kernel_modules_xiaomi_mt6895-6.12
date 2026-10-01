// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

/**
 * @file    gpueb_reserved_mem.c
 * @brief   Reserved memory info init for GPUEB
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/pm_runtime.h>
#include <mboot_params.h>
#include <linux/of_reserved_mem.h>
#include <linux/vmalloc.h>

#include "gpueb_helper.h"
#include "gpueb_reserved_mem.h"

phys_addr_t gpueb_mem_base_phys;
phys_addr_t gpueb_mem_base_virt;
phys_addr_t gpueb_mem_size;
int gpueb_mem_num;

struct gpueb_reserve_mblock *gpueb_reserve_mblock_ary;
const char *gpueb_reserve_mblock_ary_name[20];

int gpueb_reserve_mem_of_init(struct reserved_mem *rmem)
{
	gpueb_log_d(GPUEB_TAG, "%pa %pa", &rmem->base, &rmem->size);
	gpueb_mem_base_phys = (phys_addr_t) rmem->base;
	gpueb_mem_size = (phys_addr_t) rmem->size;

	return 0;
}
RESERVEDMEM_OF_DECLARE(gpueb_reserve_mem_init,
		GPUEB_MEM_RESERVED_KEY, gpueb_reserve_mem_of_init);

phys_addr_t gpueb_get_reserve_mem_phys(unsigned int id)
{
	if (id >= gpueb_mem_num) {
		gpueb_log_d(GPUEB_TAG, "no reserve memory for %d", id);
		return 0;
	} else
		return gpueb_reserve_mblock_ary[id].start_phys;
}
EXPORT_SYMBOL_GPL(gpueb_get_reserve_mem_phys);

phys_addr_t gpueb_get_reserve_mem_virt(unsigned int id)
{
	if (id >= gpueb_mem_num) {
		gpueb_log_d(GPUEB_TAG, "no reserve memory for %d", id);
		return 0;
	} else
		return gpueb_reserve_mblock_ary[id].start_virt;
}
EXPORT_SYMBOL_GPL(gpueb_get_reserve_mem_virt);

phys_addr_t gpueb_get_reserve_mem_size(unsigned int id)
{
	if (id >= gpueb_mem_num) {
		gpueb_log_d(GPUEB_TAG, "no reserve memory for %d", id);
		return 0;
	} else
		return gpueb_reserve_mblock_ary[id].size;
}
EXPORT_SYMBOL_GPL(gpueb_get_reserve_mem_size);

phys_addr_t gpueb_get_reserve_mem_phys_by_name(char *mem_id_name)
{
	int id = -1;
	int i;

	for (i = 0; i < gpueb_mem_num; i++) {
		if (!strcmp(gpueb_reserve_mblock_ary_name[i], mem_id_name))
			id = i;
	}

	if (id < 0 || id >= gpueb_mem_num) {
		gpueb_log_d(GPUEB_TAG, "no reserve memory for %d (%s)", id, mem_id_name);
		return 0;
	} else
		return gpueb_reserve_mblock_ary[id].start_phys;
}
EXPORT_SYMBOL_GPL(gpueb_get_reserve_mem_phys_by_name);

phys_addr_t gpueb_get_reserve_mem_virt_by_name(char *mem_id_name)
{
	int id = -1;
	int i;

	for (i = 0; i < gpueb_mem_num; i++) {
		if (!strcmp(gpueb_reserve_mblock_ary_name[i], mem_id_name))
			id = i;
	}

	if (id < 0 || id >= gpueb_mem_num) {
		gpueb_log_d(GPUEB_TAG, "no reserve memory for %d (%s)", id, mem_id_name);
		return 0;
	} else
		return gpueb_reserve_mblock_ary[id].start_virt;
}
EXPORT_SYMBOL_GPL(gpueb_get_reserve_mem_virt_by_name);

phys_addr_t gpueb_get_reserve_mem_size_by_name(char *mem_id_name)
{
	int id = -1;
	int i;

	for (i = 0; i < gpueb_mem_num; i++) {
		if (!strcmp(gpueb_reserve_mblock_ary_name[i], mem_id_name))
			id = i;
	}

	if (id < 0 || id >= gpueb_mem_num) {
		gpueb_log_d(GPUEB_TAG, "no reserve memory for %d (%s)", id, mem_id_name);
		return 0;
	} else
		return gpueb_reserve_mblock_ary[id].size;
}
EXPORT_SYMBOL_GPL(gpueb_get_reserve_mem_size_by_name);

int gpueb_reserved_mem_init(struct platform_device *pdev)
{
	struct device_node *of_gpueb = pdev->dev.of_node;
	unsigned int i, m_idx, m_size;
	phys_addr_t accumlate_memory_size = 0;
	int ret;
	const char *prop_table = "gpueb_mem_table";
	const char *prop_name_table = "gpueb_mem_name_table";

	if (of_property_read_u64(of_gpueb, "gpueb_mem_addr", &gpueb_mem_base_phys))
		of_property_read_u64(of_gpueb, "gpueb-mem-addr", &gpueb_mem_base_phys);
	if (of_property_read_u64(of_gpueb, "gpueb_mem_size", &gpueb_mem_size))
		of_property_read_u64(of_gpueb, "gpueb-mem-size", &gpueb_mem_size);

	/* Fallback for MT6895 default GPUEB reserved memory */
	if (!gpueb_mem_base_phys || !gpueb_mem_size) {
		pr_info(GPUEB_TAG " using default MT6895 gpueb reserved mem\n");
		gpueb_mem_base_phys = 0x7c600000;
		gpueb_mem_size = 0x200000;
	}

	pr_info(GPUEB_TAG " base_phys = 0x%llx, size = 0x%llx\n",
		(u64)gpueb_mem_base_phys, (u64)gpueb_mem_size);

	if ((gpueb_mem_base_phys >= 0x800000000ULL) || (gpueb_mem_base_phys < 0x40000000ULL)) {
		pr_err(GPUEB_TAG " Error: Wrong Address (0x%llx)\n", (u64)gpueb_mem_base_phys);
		return -1;
	}

	/* Detect property naming: support both underscore and hyphen */
	if (of_find_property(pdev->dev.of_node, "gpueb-mem-table", NULL))
		prop_table = "gpueb-mem-table";
	else if (of_find_property(pdev->dev.of_node, "gpueb_mem_table", NULL))
		prop_table = "gpueb_mem_table";

	if (of_find_property(pdev->dev.of_node, "gpueb-mem-name-table", NULL))
		prop_name_table = "gpueb-mem-name-table";
	else if (of_find_property(pdev->dev.of_node, "gpueb_mem_name_table", NULL))
		prop_name_table = "gpueb_mem_name_table";

	// Set reserved memory table
	gpueb_mem_num = of_property_count_u32_elems(
			pdev->dev.of_node,
			prop_table)
			/ MEMORY_TBL_ELEM_NUM;
	if (gpueb_mem_num <= 0) {
		pr_warn(GPUEB_TAG " %s not found, using MT6895 default table\n", prop_table);
		gpueb_mem_num = 2;
		gpueb_reserve_mblock_ary_name[0] = "MEM_ID_GPUFREQ";
		gpueb_reserve_mblock_ary_name[1] = "MEM_ID_LOG";
		gpueb_reserve_mblock_ary = vzalloc(sizeof(struct gpueb_reserve_mblock) * gpueb_mem_num);
		if (!gpueb_reserve_mblock_ary)
			return -ENOMEM;
		gpueb_reserve_mblock_ary[0].num = 0;
		gpueb_reserve_mblock_ary[0].size = 0x1000;
		gpueb_reserve_mblock_ary[1].num = 1;
		gpueb_reserve_mblock_ary[1].size = 0x180000;
	} else {
		// Get reserved mblock name
		ret = of_property_read_string_array(pdev->dev.of_node,
				prop_name_table,
				gpueb_reserve_mblock_ary_name,
				gpueb_mem_num);
		if (ret < 0) {
			pr_err(GPUEB_TAG " %s not found\n", prop_name_table);
			return -1;
		}

		gpueb_reserve_mblock_ary = vzalloc(sizeof(struct gpueb_reserve_mblock) * gpueb_mem_num);
		if (!gpueb_reserve_mblock_ary)
			return -ENOMEM;

		for (i = 0; i < gpueb_mem_num; i++) {
			ret = of_property_read_u32_index(pdev->dev.of_node,
					prop_table,
					i * MEMORY_TBL_ELEM_NUM,
					&m_idx);
			if (ret) {
				pr_err(GPUEB_TAG " Cannot get memory index(%d)\n", i);
				return -1;
			}
			gpueb_reserve_mblock_ary[m_idx].num = m_idx;

			ret = of_property_read_u32_index(pdev->dev.of_node,
					prop_table,
					(i * MEMORY_TBL_ELEM_NUM) + 1,
					&m_size);
			if (ret) {
				pr_err(GPUEB_TAG " Cannot get memory size(%d)\n", i);
				return -1;
			}

			if (m_idx >= gpueb_mem_num) {
				pr_warn(GPUEB_TAG " Skip unexpected index, %d\n", m_idx);
				continue;
			}

			gpueb_reserve_mblock_ary[m_idx].size = m_size;
			pr_info(GPUEB_TAG " Reserved block <%d  %d>\n", m_idx, m_size);
		}
	}

	for (i = 0; i < gpueb_mem_num; i++) {
		pr_info(GPUEB_TAG " gpueb_reserve_mblock_ary_name[%d] = %s\n",
			i, gpueb_reserve_mblock_ary_name[i]);
	}

	// Transfer physical address to virtual address
	gpueb_mem_base_virt = (phys_addr_t)(size_t)ioremap_wc(
			gpueb_mem_base_phys, gpueb_mem_size);
	pr_info(GPUEB_TAG " Reserved phy_base = 0x%llx, len:0x%llx, Reserved virt_base = 0x%llx\n",
		(u64)gpueb_mem_base_phys, (u64)gpueb_mem_size, (u64)gpueb_mem_base_virt);

	// Init the access address for each block
	for (i = 0; i < gpueb_mem_num; i++) {
		gpueb_reserve_mblock_ary[i].start_phys = gpueb_mem_base_phys +
			accumlate_memory_size;
		gpueb_reserve_mblock_ary[i].start_virt = gpueb_mem_base_virt +
			accumlate_memory_size;
		accumlate_memory_size += gpueb_reserve_mblock_ary[i].size;
		pr_info(GPUEB_TAG " Reserved block[%d] phys:0x%llx, virt:0x%llx, len:0x%llx\n",
			i, (u64)gpueb_reserve_mblock_ary[i].start_phys,
			(u64)gpueb_reserve_mblock_ary[i].start_virt, (u64)gpueb_reserve_mblock_ary[i].size);
	}

	if (accumlate_memory_size > gpueb_mem_size)
		pr_warn(GPUEB_TAG " Total memory in memory table is more than reserved\n");

	return 0;
}
