#!/usr/bin/env python3
"""One-time Spotify login for the Tidbyt's "spotify" app.

Runs the PKCE authorization flow in your browser, then sends the client ID and
refresh token to the device (POST /api/spotify). Tokens are not printed.

Before running:
  1. Create an app at https://developer.spotify.com/dashboard (needs Premium).
  2. Add the redirect URI  http://127.0.0.1:8888/callback  and tick "Web API".

Usage: python tools/spotify_auth.py --device <device-ip> --client-id <id>
"""

import argparse
import base64
import hashlib
import json
import secrets
import sys
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-currently-playing user-read-playback-state"


def wait_for_code(expected_state):
    result = {}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            result.update({k: v[0] for k, v in query.items()})
            ok = "code" in result and result.get("state") == expected_state
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            msg = "Spotify connected - you can close this tab." if ok else "Login failed - see the terminal."
            self.wfile.write(f"<p style='font:18px system-ui'>{msg}</p>".encode())

        def log_message(self, *args):
            pass

    server = HTTPServer(("127.0.0.1", 8888), Handler)
    while "code" not in result and "error" not in result:
        server.handle_request()
    server.server_close()
    if "error" in result:
        sys.exit(f"Spotify returned an error: {result['error']}")
    if result.get("state") != expected_state:
        sys.exit("State mismatch - aborting.")
    return result["code"]


def post_form(url, fields):
    req = urllib.request.Request(url, data=urllib.parse.urlencode(fields).encode(),
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", required=True, help="Tidbyt IP address or host name")
    ap.add_argument("--client-id", required=True, help="Client ID of your Spotify app")
    args = ap.parse_args()

    verifier = secrets.token_urlsafe(64)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    state = secrets.token_urlsafe(16)
    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "client_id": args.client_id, "response_type": "code", "redirect_uri": REDIRECT_URI,
        "code_challenge_method": "S256", "code_challenge": challenge, "scope": SCOPES, "state": state,
    })
    print("Opening Spotify login in your browser...")
    webbrowser.open(url)
    code = wait_for_code(state)

    tokens = post_form("https://accounts.spotify.com/api/token", {
        "grant_type": "authorization_code", "code": code, "redirect_uri": REDIRECT_URI,
        "client_id": args.client_id, "code_verifier": verifier,
    })
    if "refresh_token" not in tokens:
        sys.exit(f"No refresh token in response: {tokens.get('error_description', tokens)}")

    req = urllib.request.Request(
        f"http://{args.device}:8080/api/spotify",
        data=json.dumps({"client_id": args.client_id, "refresh_token": tokens["refresh_token"]}).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=10) as r:
        print("Device:", r.read().decode())
    print("Done. Play something on Spotify - it shows up in the rotation within a cycle.")


if __name__ == "__main__":
    main()
