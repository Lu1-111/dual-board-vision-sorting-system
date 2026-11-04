#!/usr/bin/env bash
set -Eeuo pipefail

# One-time deployment for the USB SD card identified on 2026-10-10.
# Existing BOOT data and the first MiB must already be backed up.
device=/dev/sdb
work=/home/uisrc/dr1_sd_work
source_dir=/home/uisrc/uisrc-lab-anlogic/boards/DR1M/buildroot
boot_source="$source_dir/images/boot"
rootfs_source="$source_dir/output/rootfs.tar.gz"
app_source=/home/uisrc/hdmi_ui_deploy
qt_source=/home/uisrc/uisrc-lab-qt/applications/qt_play/qt_lib
boot_mount=/mnt/dr1-boot
rootfs_mount=/mnt/dr1-rootfs

cleanup() {
    sync
    if mountpoint -q "$rootfs_mount"; then umount "$rootfs_mount"; fi
    if mountpoint -q "$boot_mount"; then umount "$boot_mount"; fi
}
trap cleanup EXIT

[[ $(id -u) -eq 0 ]] || { echo 'Run as root' >&2; exit 1; }
[[ $(cat /sys/block/sdb/removable) == 1 ]] || { echo 'Target is not removable' >&2; exit 1; }
[[ $(lsblk -dn -o TRAN "$device" | tr -d ' ') == usb ]] || { echo 'Target is not USB' >&2; exit 1; }
[[ $(blkid -s UUID -o value "${device}1") == 7E51-0F1E ]] || { echo 'BOOT UUID does not match the inspected SD card' >&2; exit 1; }
[[ -f "$work/boot-original.tar" && -f "$work/sd-first-mib.bin" ]] || { echo 'Original SD backup is missing' >&2; exit 1; }
[[ $(sha256sum "$work/boot-original.tar" | cut -d' ' -f1) == ddddc85889b576fd3f87bfec548564732030898210c5e578d634ba1ff5e123b2 ]] || { echo 'BOOT backup checksum failed' >&2; exit 1; }
[[ $(sha256sum "$work/sd-first-mib.bin" | cut -d' ' -f1) == 90130d51661bca790c8512c241ba23ef83cc0ae6799af6c1a38716fa1dcf7eb3 ]] || { echo 'MBR backup checksum failed' >&2; exit 1; }
for name in BOOT.bin boot.scr system.dtb uImage.lz4 uInitrd.lz4; do
    [[ -s "$boot_source/$name" ]] || { echo "Missing $name" >&2; exit 1; }
done
[[ -s "$rootfs_source" && -x "$app_source/hdmi_ui" && -d "$app_source/fonts" && -d "$qt_source" ]] || { echo 'Missing rootfs or app payload' >&2; exit 1; }

echo 'Unmounting the inspected SD card...'
while read -r mounted; do
    [[ -z "$mounted" ]] || umount "$mounted"
done < <(findmnt -rn -S "${device}1" -o TARGET || true)

echo 'Creating 256 MiB BOOT and remaining ext4 rootfs partitions...'
parted -s "$device" mklabel msdos
parted -s "$device" mkpart primary fat32 1MiB 256MiB
parted -s "$device" set 1 boot on
parted -s "$device" mkpart primary ext4 256MiB 100%
partprobe "$device"
udevadm settle
[[ -b "${device}1" && -b "${device}2" ]] || { echo 'Partitions did not appear' >&2; exit 1; }
mkfs.vfat -F 32 -n BOOT "${device}1"
mkfs.ext4 -F -L rootfs "${device}2"
udevadm settle

for part in "${device}1" "${device}2"; do
    while read -r mounted; do
        [[ -z "$mounted" ]] || umount "$mounted"
    done < <(findmnt -rn -S "$part" -o TARGET || true)
done
mkdir -p "$boot_mount" "$rootfs_mount"
mount -t vfat "${device}1" "$boot_mount"
mount -t ext4 "${device}2" "$rootfs_mount"

echo 'Installing the system root filesystem...'
tar --numeric-owner -xzpf "$rootfs_source" -C "$rootfs_mount"
[[ -x "$rootfs_mount/sbin/init" ]] || { echo 'Extracted rootfs has no init' >&2; exit 1; }
if [[ -f "$rootfs_mount/etc/fstab" ]]; then
    sed -i -E 's@^(/dev/root[[:space:]]+/[[:space:]]+)ext2([[:space:]])@\1ext4\2@' "$rootfs_mount/etc/fstab"
fi

echo 'Installing the Qt task manager...'
install -d -m 0755 "$rootfs_mount/home/uisrc/qt_play"
install -m 0755 "$app_source/hdmi_ui" "$rootfs_mount/home/uisrc/qt_play/hdmi_ui"
cp -a "$app_source/fonts" "$rootfs_mount/home/uisrc/qt_play/"
cp -a "$qt_source" "$rootfs_mount/home/uisrc/qt_play/qt_lib"

cat > "$rootfs_mount/home/uisrc/qt_play/run_hdmi_ui.sh" <<'RUN_EOF'
#!/bin/sh
cd /home/uisrc/qt_play || exit 1
QTLIB=/home/uisrc/qt_play/qt_lib
export LD_LIBRARY_PATH="$QTLIB/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$QTLIB/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QTLIB/plugins/platforms"
export QT_QPA_FONTDIR="$QTLIB/lib/fonts"
exec env QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0 ./hdmi_ui
RUN_EOF
chmod 0755 "$rootfs_mount/home/uisrc/qt_play/run_hdmi_ui.sh"

echo 'Installing the Buildroot boot files...'
for name in BOOT.bin system.dtb uImage.lz4 uInitrd.lz4; do
    cp "$boot_source/$name" "$boot_mount/$name"
done
cp "$boot_source/boot.scr" "$boot_mount/boot-initrd.scr"
partuuid=$(blkid -s PARTUUID -o value "${device}2")
[[ -n "$partuuid" ]] || { echo 'rootfs PARTUUID is missing' >&2; exit 1; }
cat > "$work/boot-ext4.cmd" <<'BOOT_EOF'
mmc part
load ${devtype} ${devnum}:${distro_bootpart} 0x10000000 ${prefix}uImage.lz4
load ${devtype} ${devnum}:${distro_bootpart} 0x14000000 ${prefix}system.dtb
setenv bootargs console=ttyS0,115200n8 earlycon=uart,mmio32,0xf8401000 loglevel=8 mtdparts=alspinor:1M(boot),-(data) root=PARTUUID=@ROOT_PARTUUID@ rootwait rootfstype=ext4 rw
bootm 0x10000000 - 0x14000000
BOOT_EOF
sed -i "s/@ROOT_PARTUUID@/$partuuid/" "$work/boot-ext4.cmd"
mkimage -A arm64 -O linux -T script -C none -n 'DR1M SD ext4 rootfs' -d "$work/boot-ext4.cmd" "$boot_mount/boot.scr" >/dev/null

echo 'Verifying deployed files...'
for name in BOOT.bin system.dtb uImage.lz4 uInitrd.lz4; do
    cmp "$boot_source/$name" "$boot_mount/$name"
done
cmp "$app_source/hdmi_ui" "$rootfs_mount/home/uisrc/qt_play/hdmi_ui"
[[ -f "$rootfs_mount/home/uisrc/qt_play/fonts/LICENSE.txt" ]]
[[ -f "$rootfs_mount/home/uisrc/qt_play/qt_lib/plugins/platforms/libqlinuxfb.so" ]]
[[ -f "$rootfs_mount/home/uisrc/qt_play/qt_lib/plugins/imageformats/libqjpeg.so" ]]
dumpimage -l "$boot_mount/boot.scr"
lsblk -o NAME,SIZE,FSTYPE,LABEL,PARTUUID,MOUNTPOINT "$device"
sync
echo "FLASH_COMPLETE rootfs_PARTUUID=$partuuid"
