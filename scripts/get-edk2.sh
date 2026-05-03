#! /bin/sh

set -e

if [ $# -ne 1 ]; then
  echo "usage: $0 <repo_root>" >&2
  exit 2
fi

root="$1"

rm -rf "$root/edk2-ovmf"

curl -L https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz \
  | gunzip | tar -xf - -C "$root"
