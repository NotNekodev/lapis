#!/bin/sh

set -e

# usage: make-initrd.sh <initrd_include_dir> <output_path>

if [ $# -ne 2 ]; then
  echo "usage: $0 <initrd_include_dir> <output_path>" >&2
  exit 2
fi

include="$1"
output="$2"

(
    cd "$include"
    find . -print0 | cpio --null -o -H newc
) > "$output"