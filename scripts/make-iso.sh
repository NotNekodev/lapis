#! /bin/sh

set -e

if [ $# -ne 4 ]; then
  echo "usage: $0 <repo_root> <build_dir> <kernel_path> <iso_output>" >&2
  exit 2
fi

root="$1"
build="$2"
kernel="$3"
output="$4"

iso_root="$build/iso_root"

rm -rf "$iso_root"
mkdir -p "$iso_root/boot/limine" "$iso_root/EFI/BOOT"

cp -v "$kernel" "$iso_root/boot/lapis"
cp -v "$build/initrd.cpio" "$iso_root/boot/initrd.cpio"
cp -v "$root/config/limine.conf" \
  "$root/limine-binary/limine-bios.sys" \
  "$root/limine-binary/limine-bios-cd.bin" \
  "$root/limine-binary/limine-uefi-cd.bin" \
  "$iso_root/boot/limine/"
cp -v "$root/limine-binary/BOOTX64.EFI" "$iso_root/EFI/BOOT/"
cp -v "$root/limine-binary/BOOTIA32.EFI" "$iso_root/EFI/BOOT/"

xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
  -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
  -efi-boot-part --efi-boot-image --protective-msdos-label \
  "$iso_root" -o "$output"

"$root/limine-binary/limine" bios-install "$output"

rm -rf "$iso_root"
