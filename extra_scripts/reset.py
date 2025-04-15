import os.path
import requests

Import("env")

PRODUCTION_VERSION = {
    "tidbyt": "v10/35833",
    "pixoticker": "v10/35833",
    "tronbyt-S3": "v10/35833",
    "tidbyt-gen2": "v11/35369",
    "matrixportal-s3": "v11/35369" # just to shut up the errors.
}[env["PIOENV"]]

def fetch_firmware():

    binaries = [
        "firmware.bin",
        "partitions.bin",
        "bootloader.bin"
    ]

    if not os.path.exists(f".production/{PRODUCTION_VERSION}"):
        os.makedirs(f".production/{PRODUCTION_VERSION}")

    for binary in binaries:
        if os.path.exists(f".production/{PRODUCTION_VERSION}/{binary}"):
            continue

        r = requests.get(f"https://storage.googleapis.com/tidbyt-public-firmware/{PRODUCTION_VERSION}/{binary}")
        if r.status_code != 200:
            raise Exception(f"Failed to fetch {binary}: {r.status_code}")

        with open(f".production/{PRODUCTION_VERSION}/{binary}", "wb") as f:
            f.write(r.content)

fetch_firmware()

platform = env.PioPlatform()
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")

env.AutodetectUploadPort()

env.Replace(
    RESET_FIRMWARE_VERSION=PRODUCTION_VERSION,
    RESET_UPLOADER=os.path.join(
        platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
    RESET_UPLOADERFLAGS=[
        "--chip", mcu,
        "--port", '"$UPLOAD_PORT"',
        "--baud", "$UPLOAD_SPEED",
        "--before", board.get("upload.before_reset", "default_reset"),
        "--after", board.get("upload.after_reset", "hard_reset"),
        "write_flash", "-z",
        "--flash_mode", "${__get_board_flash_mode(__env__)}",
        "--flash_freq", "${__get_board_f_flash(__env__)}",
        "--flash_size", board.get("upload.flash_size", "detect"),
        "0x01000",
        "$PROJECT_DIR/.production/$RESET_FIRMWARE_VERSION/bootloader.bin",
        "0x08000",
        "$PROJECT_DIR/.production/$RESET_FIRMWARE_VERSION/partitions.bin",
        "0x10000",
        "$PROJECT_DIR/.production/$RESET_FIRMWARE_VERSION/firmware.bin",
    ],
    RESET_UPLOADCMD='"$PYTHONEXE" "$RESET_UPLOADER" $RESET_UPLOADERFLAGS'
)

env.AddCustomTarget("reset", None, actions=['$RESET_UPLOADCMD'])
