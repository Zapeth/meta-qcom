
SOC_FAMILY = "mdm9640"
MACHINE = "mdm9640"
ABOOT_BOARD = "9640"
require conf/machine/include/soc-family.inc
require conf/machine/include/arm/armv7a/tune-cortexa8.inc

QCOM_BOOTIMG_KERNEL_BASE ?= "0x80000000"
QCOM_BOOTIMG_PAGE_SIZE ?= "4096"
KERNEL_TAGS_ADDR ?= "0x81E00000"

# Uncomment the following line to enable the hard floating point abi. Note that
# this breaks some binary libraries and 3D (neither of which ship with
# meta-yocto). For maximum compatibility, leave this disabled.
#DEFAULTTUNE ?= "cortexa8hf-neon"

# Image Generation
# Make ubi filesystem and then pack it in a ubi container, as stock
IMAGE_FSTYPES ?= "ubi multiubi ext3.gz cpio.gz"

# Set the volume name inside the ubi image
UBI_VOLNAME = "rootfs"
ROOT_FLASH_SIZE = "63"
SYSTEM_VOLUME_SIZE ?= "63MiB"
CACHE_VOLUME_SIZE ?= "55MiB"

# Max supported inodes
EXTRA_IMAGECMD ?= "-N 4096"

MKUBIFS_ARGS ?="-m 4096 -e 253952 -c 2146 -F"
UBINIZE_ARGS ?="-m 4096 -p 256KiB -s 4096"
MULTIUBI_BUILD ?= ""