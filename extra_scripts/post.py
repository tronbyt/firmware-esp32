#!/usr/bin/env python3
"""
Post-upload script for PlatformIO

This script runs after successful firmware upload and performs the following actions:
1. Renames secrets.json to secrets.json.injected to indicate successful injection
2. Preserves previous injected files with timestamps
3. Provides clear feedback about the secrets injection status

Usage:
- Automatically runs after: pio run -e <env> --target upload
- Helps track which secrets have been injected into firmware
- Prevents accidental reuse of the same secrets file

To reuse secrets: cp secrets.json.injected secrets.json
"""

import os
import shutil
from datetime import datetime

Import("env")


def rename_secrets_after_upload(*args, **kwargs):
    """
    Post-upload action to rename secrets.json to secrets.json.injected
    This indicates that the secrets have been successfully injected into the firmware.
    Adds a syntax error comment to force users to read warnings when reusing.
    """
    secrets_file = "secrets.json"
    injected_file = "secrets.json.injected"

    if os.path.exists(secrets_file):
        try:
            # Add timestamp to the injected file for tracking
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            timestamped_file = f"secrets.json.injected.{timestamp}"

            # If an injected file already exists, rename it with timestamp
            if os.path.exists(injected_file):
                print(f"Previous {injected_file} found, renaming to {timestamped_file}")
                shutil.move(injected_file, timestamped_file)

            # Read the original secrets file
            with open(secrets_file, 'r') as f:
                original_content = f.read()

            # Create content with intentional JSON syntax error
            # Add a trailing comma and comment that breaks JSON parsing
            syntax_error_content = f'''{{
    "WARNING_REMOVE_THIS_LINE_TO_USE": "If you modified secrets, ERASE DEVICE FIRST: pio run -e <env> -t erase",
    // INTENTIONAL SYNTAX ERROR: Remove this comment line and the line above to use
{original_content[1:-1] if len(original_content) >= 2 else ''},
    "INJECTED_TIMESTAMP": "{timestamp}",
}}'''

            # Write the content with syntax error to injected file
            with open(injected_file, 'w') as f:
                f.write(syntax_error_content)

            # Remove the original file
            os.remove(secrets_file)

            print(f"✓ Successfully created {injected_file} with deployment tracking")
            print(f"  This indicates secrets have been injected into the firmware.")
            print(f"  To reuse: copy {injected_file} to {secrets_file} and remove the warning line")

        except Exception as e:
            print(f"Warning: Failed to process {secrets_file}: {e}")
            # Fallback to simple rename if JSON processing fails
            try:
                shutil.move(secrets_file, injected_file)
                print(f"✓ Fallback: renamed {secrets_file} to {injected_file}")
            except Exception as e2:
                print(f"Warning: Fallback rename also failed: {e2}")
    else:
        print(f"No {secrets_file} file found - nothing to rename")


def main():
    """
    Main function that sets up the post-upload action
    """
    # Add post-upload action to rename secrets file
    env.AddPostAction("upload", rename_secrets_after_upload)


# Execute main function
main()
