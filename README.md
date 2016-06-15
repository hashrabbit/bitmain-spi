# Bitmain SPI driver

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
