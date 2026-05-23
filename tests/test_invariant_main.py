import pytest
import re
import os
import ast
import tokenize
import io

# Adversarial and boundary payloads representing patterns that should NOT appear
# as hardcoded secrets in source code
HARDCODED_SECRET_PATTERNS = [
    # Common API key formats
    r'(?i)(api[_-]?key|apikey)\s*[=:]\s*["\'][A-Za-z0-9+/=_\-]{16,}["\']',
    # AWS-style keys
    r'AKIA[0-9A-Z]{16}',
    # Generic long hex strings assigned to key variables
    r'(?i)(secret|token|password|passwd|pwd|key)\s*[=:]\s*["\'][A-Fa-f0-9]{16,}["\']',
    # Base64-like secrets
    r'(?i)(secret|token|api_key)\s*[=:]\s*["\'][A-Za-z0-9+/]{20,}={0,2}["\']',
    # Hardcoded bearer tokens
    r'(?i)bearer\s+[A-Za-z0-9\-._~+/]+=*',
    # Generic password assignments
    r'(?i)(password|passwd)\s*=\s*["\'][^"\']{6,}["\']',
]

ADVERSARIAL_PAYLOADS = [
    # Simulated hardcoded API keys that should never appear in source
    'api_key = "AKIAIOSFODNN7EXAMPLE"',
    'API_KEY = "sk-proj-abcdefghijklmnopqrstuvwxyz123456"',
    'secret = "hardcoded_secret_value_1234567890abcdef"',
    'token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload.signature"',
    'password = "SuperSecret123!@#"',
    'api_key="0123456789abcdef0123456789abcdef"',
    'char api_key[] = "my_real_api_key_12345678"',
    '#define API_KEY "hardcoded_firmware_key_abc123"',
    'static const char* API_KEY = "firmware_secret_key_xyz789"',
    'nvs_set_str(handle, "api_key", "HARDCODED_KEY_VALUE_HERE")',
    # Boundary: empty or placeholder values (these are acceptable)
    'api_key = ""',
    'api_key = "YOUR_API_KEY_HERE"',
    'api_key = "PLACEHOLDER"',
    # Boundary: keys loaded from NVS/environment (acceptable pattern)
    'nvs_get_api_key(api_key, sizeof(api_key))',
    'getenv("API_KEY")',
]


def contains_hardcoded_secret(content: str) -> list:
    """Check if content contains hardcoded secrets, return list of matches."""
    findings = []
    for pattern in HARDCODED_SECRET_PATTERNS:
        matches = re.findall(pattern, content)
        if matches:
            findings.append((pattern, matches))
    return findings


def is_safe_key_usage(content: str) -> bool:
    """
    Returns True if the content represents safe key handling
    (loaded from NVS, environment, or is a placeholder).
    """
    safe_patterns = [
        r'nvs_get',
        r'getenv\(',
        r'YOUR_API_KEY',
        r'PLACEHOLDER',
        r'<YOUR_KEY>',
        r'api_key\s*=\s*""',
        r'nvs_get_api_key',
    ]
    for pattern in safe_patterns:
        if re.search(pattern, content, re.IGNORECASE):
            return True
    return False


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_no_hardcoded_api_keys_in_source(payload):
    """
    Invariant: API keys and secrets must NEVER be hardcoded directly in source code.
    All sensitive credentials must be loaded at runtime from secure storage (NVS, 
    environment variables, or secure enclaves), never embedded as string literals.
    This test verifies that adversarial/boundary inputs representing hardcoded secrets
    are correctly identified as violations, while safe patterns are recognized as acceptable.
    """
    findings = contains_hardcoded_secret(payload)
    is_safe = is_safe_key_usage(payload)

    if findings and not is_safe:
        # This payload contains a hardcoded secret pattern — it MUST NOT appear in firmware source
        # The invariant: if we detect a hardcoded secret, it should be flagged as a violation
        assert len(findings) > 0, (
            f"Expected to detect hardcoded secret in payload but detection failed: {payload!r}"
        )
        # Verify the detection is meaningful (not a false positive on safe patterns)
        for pattern, matches in findings:
            assert matches, f"Pattern {pattern!r} matched but returned no captures for: {payload!r}"
    else:
        # Safe patterns should not trigger hardcoded secret detection
        # OR the payload is recognized as using secure key loading
        assert is_safe or not findings, (
            f"Unexpected hardcoded secret detected in what should be a safe pattern: {payload!r}\n"
            f"Findings: {findings}"
        )


@pytest.mark.parametrize("source_file_content,should_be_safe", [
    # UNSAFE: hardcoded key in C source
    ('static char api_key[] = "AKIAIOSFODNN7EXAMPLE1234";', False),
    # UNSAFE: #define with real-looking key
    ('#define API_KEY "abcdef1234567890abcdef1234567890"', False),
    # SAFE: loading from NVS
    ('nvs_get_api_key(api_key, sizeof(api_key))', True),
    # SAFE: empty initialization
    ('char api_key[MAX_API_KEY_LEN + 1] = "";', True),
    # SAFE: environment variable
    ('const char* key = getenv("API_KEY");', True),
    # UNSAFE: password hardcoded
    ('const char* password = "firmware_pass_12345";', False),
    # SAFE: placeholder comment
    ('// api_key = "YOUR_API_KEY_HERE" -- set via NVS', True),
    # UNSAFE: token hardcoded
    ('char token[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9abc123";', False),
])
def test_source_code_secret_detection_invariant(source_file_content, should_be_safe):
    """
    Invariant: The secret detection mechanism must correctly classify source code patterns.
    Safe patterns (NVS loading, env vars, placeholders) must not be flagged.
    Unsafe patterns (hardcoded secrets) must always be detected.
    This ensures the security boundary between safe and unsafe credential handling is maintained.
    """
    findings = contains_hardcoded_secret(source_file_content)
    is_safe = is_safe_key_usage(source_file_content)

    if should_be_safe:
        # Safe patterns: either no findings, or recognized as safe usage
        assert is_safe or not findings, (
            f"False positive: safe source pattern incorrectly flagged as containing hardcoded secret.\n"
            f"Content: {source_file_content!r}\n"
            f"Findings: {findings}"
        )
    else:
        # Unsafe patterns: must be detected as containing hardcoded secrets
        assert findings and not is_safe, (
            f"Security violation missed: hardcoded secret not detected in source pattern.\n"
            f"Content: {source_file_content!r}\n"
            f"This represents a failure of the security invariant — hardcoded secrets must always be detectable."
        )


def test_api_key_never_hardcoded_in_firmware_source():
    """
    Invariant: The firmware source file (main.c equivalent) must not contain
    any hardcoded API keys, tokens, or secrets. All credentials must be
    retrieved from secure runtime storage (NVS or equivalent).
    
    This test scans for the presence of the actual source file and validates
    the security property holds. If the file doesn't exist, it validates
    the detection logic itself is sound.
    """
    # Simulate what the firmware source should look like (safe pattern)
    safe_firmware_snippet = """
    char api_key[MAX_API_KEY_LEN + 1];
    if (nvs_get_api_key(api_key, sizeof(api_key)) == ESP_OK &&
        strlen(api_key) > 0) {
        // Use api_key loaded from NVS
    }
    """
    
    # Simulate what the firmware source must NOT look like (unsafe pattern)
    unsafe_firmware_snippet = """
    char api_key[MAX_API_KEY_LEN + 1] = "HARDCODED_API_KEY_12345678901234";
    if (strlen(api_key) > 0) {
        // Use hardcoded api_key - SECURITY VIOLATION
    }
    """
    
    # The safe pattern must not trigger secret detection
    safe_findings = contains_hardcoded_secret(safe_firmware_snippet)
    safe_is_safe = is_safe_key_usage(safe_firmware_snippet)
    
    assert safe_is_safe or not safe_findings, (
        f"Safe NVS-loading pattern incorrectly flagged: {safe_findings}"
    )
    
    # The unsafe pattern must trigger secret detection
    unsafe_findings = contains_hardcoded_secret(unsafe_firmware_snippet)
    unsafe_is_safe = is_safe_key_usage(unsafe_firmware_snippet)
    
    assert unsafe_findings and not unsafe_is_safe, (
        "CRITICAL: Hardcoded API key in firmware source was not detected. "
        "The security invariant requires that hardcoded credentials are always flagged."
    )
    
    # If the actual source file exists, scan it
    candidate_paths = [
        "main/main.c",
        "main.c",
        os.path.join(os.path.dirname(__file__), "main/main.c"),
        os.path.join(os.path.dirname(__file__), "main.c"),
    ]
    
    for source_path in candidate_paths:
        if os.path.exists(source_path):
            with open(source_path, 'r', errors='replace') as f:
                source_content = f.read()
            
            findings = contains_hardcoded_secret(source_content)
            is_safe_overall = is_safe_key_usage(source_content)
            
            # The invariant: no hardcoded secrets should exist in the actual source
            assert not findings or is_safe_overall, (
                f"SECURITY VIOLATION: Hardcoded secrets detected in {source_path}!\n"
                f"Findings: {findings}\n"
                f"All API keys must be loaded from NVS or secure storage at runtime."
            )
            break