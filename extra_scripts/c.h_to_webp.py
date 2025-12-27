import re
import argparse

# Parse command-line arguments
parser = argparse.ArgumentParser(description='Convert C header array to WebP binary file')
parser.add_argument('input_file', help='Path to the input C file containing the hex array')
parser.add_argument('-o', '--output', default='output.webp', help='Path to the output WebP file (default: output.webp)')

args = parser.parse_args()

input_c_file = args.input_file
output_webp_file = args.output

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
