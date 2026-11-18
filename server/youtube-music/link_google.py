"""Pair a personal TV OAuth client with read-only YouTube access."""
import argparse
import html
import json
import os
from pathlib import Path
import time
from urllib.parse import urlencode

import requests

parser = argparse.ArgumentParser()
parser.add_argument("--link-file", type=Path, required=True)
args = parser.parse_args()
os.umask(0o077)
state = Path(os.environ["SONIX_YTM_STATE"])
client = json.loads((state / "client.json").read_text())
response = requests.post("https://oauth2.googleapis.com/device/code", timeout=15, data={
    "client_id": client["client_id"], "scope": "https://www.googleapis.com/auth/youtube.readonly"})
response.raise_for_status()
code = response.json()
url = code["verification_url"] + "?" + urlencode({"user_code": code["user_code"]})
args.link_file.write_text('<meta charset="utf-8"><a href="' + html.escape(url) + '">Connect Sonix to Google</a>')
interval = max(5, code["interval"])
deadline = time.monotonic() + code["expires_in"]
try:
    while time.monotonic() < deadline:
        time.sleep(interval)
        token = requests.post("https://oauth2.googleapis.com/token", timeout=20, data={
            **client, "device_code": code["device_code"], "grant_type": "urn:ietf:params:oauth:grant-type:device_code"}).json()
        if token.get("error") == "authorization_pending":
            continue
        if token.get("error") == "slow_down":
            interval += 5
            continue
        if "refresh_token" not in token:
            raise SystemExit("Pairing failed: " + token.get("error", "missing refresh token"))
        token["expires_at"] = int(time.time()) + token["expires_in"]
        pending = state / "oauth.pending"
        pending.write_text(json.dumps(token))
        pending.replace(state / "oauth.json")
        print("Read-only Google access linked.")
        break
    else:
        raise SystemExit("Pairing expired")
finally:
    args.link_file.unlink(missing_ok=True)
