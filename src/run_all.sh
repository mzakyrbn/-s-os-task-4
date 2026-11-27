#!/usr/bin/env bash
set -e

# Setup
GROUP="A06" 
RUNS=3
DIR="$(cd "$(dirname "$0")" && pwd)"
LOGS="$DIR/../logs"
IMG="$DIR/lab_disk.img"
MNT="/mnt/lab"
LOGICAL=$((50 * 1024 * 1024))
TS="$(date +%Y%m%d-%H%M%S)"
CSV="$LOGS/${GROUP}-waf-${TS}.csv"

# create logs/ folder
mkdir -p "$LOGS"

# cleaning stuff
bash "$DIR/teardown_lab.sh" >/dev/null

# create Media B and connect to loop device
dd if=/dev/zero of="$IMG" bs=1M count=64 status=none
LOOP=$(sudo losetup -f --show "$IMG")
DEV=$(basename "$LOOP")
sudo mkdir -p "$MNT"
trap 'sync; sudo umount "$MNT" 2>/dev/null || true; sudo losetup -d "$LOOP" 2>/dev/null || true; sudo rm -f "$IMG"' EXIT

echo "run,fs,pattern,waf_fs,waf_block" > "$CSV"

# Format Media B with ext4 and F2FS
for fs in ext4 f2fs; do
    for mode in rand seq; do
        for r in $(seq 1 "$RUNS"); do
            if mountpoint -q "$MNT"; then
                sync
                sudo umount "$MNT"
            fi

            if [ "$fs" = "ext4" ]; then
                sudo mkfs.ext4 -F -q "$LOOP"
            else
                sudo mkfs.f2fs -f "$LOOP" >/dev/null
            fi

            sudo mount "$LOOP" "$MNT"
            sudo chown "$USER:$USER" "$MNT"

            # create 16MB file before test
            dd if=/dev/zero of="$MNT/testfile.bin" bs=1M count=16 status=none
            sync
            echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null

            # Catat nilai sebelum pengujian
            fs_b=$(cat "/sys/fs/$fs/$DEV/lifetime_write_kbytes")
            blk_b=$(awk '{print $7}' "/sys/block/$DEV/stat")

            # Jalankan fio dan simpan output di logs/
            fio "$DIR/${mode}_write.fio" --output="$LOGS/${GROUP}-fio-${fs}-${mode}-r${r}-${TS}.txt"
            sync

            # Catat nilai sesudah pengujian
            fs_a=$(cat "/sys/fs/$fs/$DEV/lifetime_write_kbytes")
            blk_a=$(awk '{print $7}' "/sys/block/$DEV/stat")

            # Hitung WAF level filesystem dan level block
            waf_fs=$(awk "BEGIN {printf \"%.4f\", (($fs_a - $fs_b) * 1024) / $LOGICAL}")
            waf_blk=$(awk "BEGIN {printf \"%.4f\", (($blk_a - $blk_b) * 512) / $LOGICAL}")

            echo "[$fs | $mode | run $r] WAF_fs=$waf_fs | WAF_block=$waf_blk"
            echo "$r,$fs,$mode,$waf_fs,$waf_blk" >> "$CSV"
        done
    done
done

echo "Finish: $CSV"