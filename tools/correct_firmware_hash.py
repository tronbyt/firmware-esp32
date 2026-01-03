#!/usr/bin/env python3
import subprocess
import sys
import struct
import argparse


def get_esptool_output(esptool_path: str, file_path: str) -> str | None:
    cmd = [esptool_path, "--chip", "esp32", "image_info", file_path]
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error running esptool: {result.stderr}")
        return None
    return result.stdout


def update_firmware_file_with_checksum(file_path: str, checksum: str) -> None:
    checksum_byte = int(checksum, 16)
    with open(file_path, "r+b") as f:
        f.seek(-33, 2)
        f.write(struct.pack("B", checksum_byte))


def update_firmware_file_with_sha256(file_path: str, sha256: str) -> None:
    sha256_bytes = bytes.fromhex(sha256)
    with open(file_path, "r+b") as f:
        # Write the SHA256 hash at the end
        f.seek(-32, 2)
        f.write(sha256_bytes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Fix firmware checksum and SHA256")
    parser.add_argument("esptool_path", help="Path to esptool.py")
    parser.add_argument("file_path", help="Path to firmware.bin")
    args = parser.parse_args()

    esptool_output = get_esptool_output(args.esptool_path, args.file_path)
    if not esptool_output:
        sys.exit(1)

    checksum_val: str | None = None
    sha256_val: str | None = None

    for line in esptool_output.splitlines():
        if "Checksum:" in line:
            if "expected" in line:
                try:
                    checksum_val = line.split("expected")[1].strip().strip(")")
                except IndexError:
                    pass
            elif "(valid)" in line:
                checksum_val = line.split()[1]

    if checksum_val:
        print(f"Detected correct checksum: {checksum_val}")
        update_firmware_file_with_checksum(args.file_path, checksum_val)
    else:
        print("Could not parse expected checksum from esptool output.")

    # Now SHA256
    esptool_output_sha = get_esptool_output(args.esptool_path, args.file_path)
    if esptool_output_sha:
        for line in esptool_output_sha.splitlines():
            if "Validation Hash:" in line:
                # "Validation Hash: ... (valid)"
                parts = line.split()
                if len(parts) >= 2:
                    sha256_val = parts[-2]

    if sha256_val:
        print(f"Detected SHA256: {sha256_val}")
        update_firmware_file_with_sha256(args.file_path, sha256_val)