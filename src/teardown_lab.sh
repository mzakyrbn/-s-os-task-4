#!/usr/bin/env bash
set -e
IMG="$(cd "$(dirname "$0")" && pwd)/lab_disk.img"
MNT="/mnt/lab"

# jika mount maka di unmount
if mountpoint -q "$MNT"; then
    sync
    sudo umount "$MNT"
fi

# jika file image ada maka lepas loop lalu hapus image
if [ -e "$IMG" ]; then
    for d in $(sudo losetup -j "$IMG" -O NAME --noheadings 2>/dev/null || true); do
        sudo losetup -d "$d"
    done
    sudo rm -f "$IMG"
fi

echo "Teardown finish"