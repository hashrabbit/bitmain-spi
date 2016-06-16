# Bitmain SPI driver

## Supported Hardware

* Antminer S5
* Antminer S7

Other models might be functional but haven't been tested.

## Building

Build with:

```
make \
  ARCH=arm \
  CROSS_COMPILE=/path/to/toolchain/usr/bin/arm-none-linux-gnueabi- \
  KERNEL_DIR=/path/to/linux-3.8.13/source
```

Install with:

```
make install \
  DESTDIR=/path/to/target \
  KERNEL_DIR=/path/to/linux-3.8.13/source
```
