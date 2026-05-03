#! /bin/sh

set -e

if [ $# -ne 4 ]; then
  echo "usage: $0 <repo_root> <kernel_path> <hdd_output> <build_dir>" >&2
  exit 2
fi

root="$1"
kernel="$2"
output="$3"
build="$4"

rm -f "$output"

dd if=/dev/zero bs=1M count=0 seek=64 of="$output"
PATH="$PATH:/usr/sbin:/sbin" sgdisk "$output" -n 1:2048 -t 1:ef00 -m 1
"$root/limine-binary/limine" bios-install "$output"

mformat -i "$output@@1M"
mmd -i "$output@@1M" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$output@@1M" "$kernel" ::/boot/lapis
mcopy -i "$output@@1M" "$root/config/limine.conf" "$root/limine-binary/limine-bios.sys" ::/boot/limine
mcopy -i "$output@@1M" "$root/limine-binary/BOOTX64.EFI" ::/EFI/BOOT
mcopy -i "$output@@1M" "$root/limine-binary/BOOTIA32.EFI" ::/EFI/BOOT
