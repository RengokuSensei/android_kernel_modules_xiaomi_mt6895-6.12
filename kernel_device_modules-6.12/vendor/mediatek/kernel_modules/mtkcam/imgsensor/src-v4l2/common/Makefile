# SPDX-License-Identifier: GPL-2.0
# 6 xaga camera sensor drivers

custom_kernel_imgsensor := $(strip $(subst ",,$(CONFIG_CUSTOM_KERNEL_IMGSENSOR)))

$(foreach sensor,$(custom_kernel_imgsensor), \
    $(eval -include $(srctree)/$(src)/$(sensor)/Makefile) \
)
