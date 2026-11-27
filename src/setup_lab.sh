#!/usr/bin/env bash
set -e

# set path
DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="$DIR/lab_disk.img"
MNT="/mnt/lab"

# clean 
bash "$DIR/teardown_lab.sh" >/dev/null

# create image disk with size of 64MB
dd if=/dev/zero of="$IMG" bs=1M count=64

# connect ke loop device
LOOP=$(sudo losetup -f --show "$IMG")

# Format loop device dengan ext4
sudo mkfs.ext4 -F -q "$LOOP"

# mount loop
sudo mkdir -p "$MNT"
sudo mount "$LOOP" "$MNT"

# so active user can use it
TARGET_USER="${SUDO_USER:-$USER}"
sudo chown "$TARGET_USER:$TARGET_USER" "$MNT"

echo "Create: $LOOP at $MNT"