import re

# Input C file containing the array
input_c_file = "/Users/tavis/code/tronbyt-firmware-http/lib/assets/noapps_webp.c"
output_webp_file = "output.webp"

# Read the C file
with open(input_c_file, "r") as f:
    c_code = f.read()

# Extract the hex values from the C array
# The regex matches sequences like \x52, \x49, etc.
hex_values = re.findall(r"\\x([0-9A-Fa-f]{2})", c_code)

# Convert hex values to binary data
binary_data = bytes(int(hex_value, 16) for hex_value in hex_values)

# Write the binary data to a .webp file
with open(output_webp_file, "wb") as f:
    f.write(binary_data)

print(f"Binary WebP file saved to {output_webp_file}")
