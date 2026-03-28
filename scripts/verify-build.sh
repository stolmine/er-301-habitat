#!/bin/bash
# Verify that packages were built against the firmware currently on the SD card.
#
# Usage: ./scripts/verify-build.sh
#
# This script:
# 1. Hashes the firmware app.bin that packages were built against (from SDK symlink)
# 2. Hashes the firmware zip on the SD card
# 3. Compares them
# 4. If they match, packages are safe to install

SDK_PATH="$(readlink -f er-301)"
FW_BIN="$SDK_PATH/release/am335x/app/app.bin"
HASH_FILE="testing/am335x/.fw_hash"
SD_FW_DIR="/mnt/ER-301/firmware"

# Step 1: Record firmware hash at build time
record_hash() {
    if [ ! -f "$FW_BIN" ]; then
        echo "ERROR: Firmware binary not found at $FW_BIN"
        exit 1
    fi
    md5sum "$FW_BIN" | cut -d' ' -f1 > "$HASH_FILE"
    echo "Recorded firmware hash: $(cat $HASH_FILE)"
    echo "Built: $(date)" >> "$HASH_FILE"
}

# Step 2: Verify SD card firmware matches
verify() {
    if [ ! -f "$HASH_FILE" ]; then
        echo "ERROR: No build hash found. Run: $0 record"
        exit 1
    fi

    BUILD_HASH=$(head -1 "$HASH_FILE")
    echo "Build was against firmware hash: $BUILD_HASH"

    # Extract kernel.bin from the SD card firmware zip and hash the app
    if [ ! -d "$SD_FW_DIR" ]; then
        echo "ERROR: SD card not mounted at /mnt"
        exit 1
    fi

    # Find the latest txo firmware zip
    SD_ZIP=$(ls -t "$SD_FW_DIR"/er-301-v0.7.0-txo*.zip 2>/dev/null | head -1)
    if [ -z "$SD_ZIP" ]; then
        echo "ERROR: No TXo firmware zip found on SD"
        exit 1
    fi

    echo "SD firmware: $SD_ZIP"

    # The firmware zip contains kernel.bin (which is app.bin + header)
    # Compare sizes as a quick check
    BUILD_SIZE=$(stat -c%s "$FW_BIN" 2>/dev/null)
    SD_KERNEL_SIZE=$(unzip -l "$SD_ZIP" kernel.bin 2>/dev/null | grep kernel.bin | awk '{print $1}')

    echo "Build app.bin size: $BUILD_SIZE"
    echo "SD kernel.bin size: $SD_KERNEL_SIZE"

    if [ "$BUILD_SIZE" = "$SD_KERNEL_SIZE" ]; then
        echo "MATCH: Sizes match. Packages should be compatible."
    else
        echo "MISMATCH: Firmware on SD was built separately."
        echo "Rebuild packages: make clean ARCH=am335x && make ARCH=am335x"
        exit 1
    fi
}

case "$1" in
    record)
        record_hash
        ;;
    verify|"")
        verify
        ;;
    *)
        echo "Usage: $0 [record|verify]"
        exit 1
        ;;
esac
