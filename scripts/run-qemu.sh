#! /bin/sh

set -e

if [ $# -lt 5 ]; then
  echo "usage: $0 <qemu> <mode> <iso> <hdd> <ovmf> [qemu_flags...]" >&2
  exit 2
fi

qemu="$1"
mode="$2"
iso="$3"
hdd="$4"
ovmf="$5"
shift 5

case "$mode" in
  iso)
    exec "$qemu" -M q35 -cdrom "$iso" -boot d "$@"
    ;;
  iso-uefi)
    exec "$qemu" -M q35 \
      -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" \
      -cdrom "$iso" -boot d "$@"
    ;;
  hdd)
    exec "$qemu" -M q35 -hda "$hdd" "$@"
    ;;
  hdd-uefi)
    exec "$qemu" -M q35 \
      -drive "if=pflash,unit=0,format=raw,file=$ovmf,readonly=on" \
      -hda "$hdd" "$@"
    ;;
  *)
    echo "unknown mode: $mode" >&2
    exit 2
    ;;
esac
