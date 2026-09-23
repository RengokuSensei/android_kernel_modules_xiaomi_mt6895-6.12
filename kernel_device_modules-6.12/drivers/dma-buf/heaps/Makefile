# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel system_heap; exports
# dma_buf_get_gid for mediatek-drm)
ccflags-y += -I$(src)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu
obj-m += system_heap.o
system_heap-y := mtk-deferred-free-helper.o mtk_cache_pool.o \
	mtk_dmabuf_monitor.o mtk_page_pool.o mtk_system_heap.o

obj-m += mtk_sec_heap.o
mtk_sec_heap-y := mtk_sec_heap_main.o mtk_tmem_interop.o
