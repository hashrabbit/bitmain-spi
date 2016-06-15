# Makefile for bitmain-spi
#
# Copyright (C) 2016 HashRabbit, Inc. - https://hashrabbit.co
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

KERNEL_MAKE_OPTS += -C $(KERNEL_DIR) \
	ARCH="$(ARCH)" \
	CROSS_COMPILE="$(CROSS_COMPILE)" \
	INSTALL_MOD_PATH="$(DESTDIR)" \
	M="$(CURDIR)"

build:
	make $(KERNEL_MAKE_OPTS) modules

install: modules_install

modules_install:
	make $(KERNEL_MAKE_OPTS) modules_install

clean:
	make $(KERNEL_MAKE_OPTS) clean

.PHONY: build install modules_install clean
