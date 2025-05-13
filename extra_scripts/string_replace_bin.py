import sys
import shutil

# Usage
file_path = ".pio/build/tidbyt/firmware.bin"
new_path = file_path.replace("firmware", "firmware_mod")
shutil.copy(file_path, new_path)


# Replace this with the string to be replaced

# new values should be the first three areuments passed to script
# extract ssid, password and url from command-line arguments
# substitutions = sys.argv[1:4]
dict = {
    "XplaceholderWIFISSID____________": sys.argv[1],
    "XplaceholderWIFIPASSWORD________________________________________": sys.argv[2],
    "XplaceholderREMOTEURL___________________________________________________________________________________________________________": sys.argv[
        3
    ],
}

with open(new_path, "r+b") as f:
    # Read the binary file into memory
    content = f.read()

    for old_string, new_string in dict.items():
        print(f"Doing {old_string}")
        # Ensure the new string is not longer than the original
        if len(new_string) > len(old_string):
            raise ValueError("Replacement string cannot be longer than the original string.")

        # Find the position of the old string
        position = content.find(old_string.encode("ascii") + b"\x00")
        if position == -1:
            raise ValueError(f"String '{old_string}' not found in the binary.")

        # Create the new string, null-terminated, and padded to match the original length
        padded_new_string = new_string + "\x00"
        padded_new_string = padded_new_string.ljust(len(old_string) + 1, '\x00')  # Add padding if needed

        # Replace the string
        f.seek(position)
        f.write(padded_new_string.encode("ascii"))
        print(f"String '{old_string}' replaced with '{new_string}'.")

# replace_string_in_esp_bin(file_path, placeholders, substitutions)
