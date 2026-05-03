#! /bin/sh

set -e

if [ $# -ne 1 ]; then
  echo "usage: $0 <repo_root>" >&2
  exit 2
fi

root="$1"

rm -rf "$root/limine-binary"

curl -L https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz \
  | gunzip | tar -xf - -C "$root"

make -C "$root/limine-binary"
