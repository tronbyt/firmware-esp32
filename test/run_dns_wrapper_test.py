import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="tronbyt-dns-test-") as scratch:
    binary = str(Path(scratch) / "test_dns_wrapper")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-D_DEFAULT_SOURCE",
        "-Wall", "-Wextra", "-Werror", "-I", str(root / "stubs"),
        str(root / "test_dns_wrapper.c"), "-o", binary,
    ], check=True)
    subprocess.run([binary], check=True)
