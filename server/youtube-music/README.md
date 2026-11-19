# YouTube Music for HiBy R1 · experimental setup

Use this guide to set up one R1 for one Google account. A Linux server runs the bridge. The R1 needs Wi-Fi to browse the online catalog and download tracks. Once a list has been saved, the R1 plays its files from the SD card with the server and Wi-Fi switched off.

```text
R1 + SD card ── HTTPS ──► Caddy ──► Sonix bridge ──► Google / YouTube Music
    │                                   │
    └── local Opus files                └── private OAuth state on the server
```

The firmware reads two lines from `youtube-music.conf` in the root of the SD card: the HTTPS base URL and a per-installation device key. You can move the bridge by updating this file. The server uses the read-only YouTube Data API for personal playlists and their contents. Liked songs, library songs and account audio use the YouTube Music TV client with a second OAuth grant. Search is available without Google pairing. The bridge downloads audio, preserves the original Opus packets and adds tags, artwork and lyrics when available. It prefers format 774, with a fallback to other available Opus formats. Account entitlement and the track determine the quality.

## Agent handoff

Obtain the operator's server/SSH destination, hostname, existing reverse proxy (if any), Google Cloud project, and the mounted R1 SD card. The operator chooses the Google account and approves both consent screens. Work as the owner of the private state directory. Keep credentials, pairing links, signed track URLs and the completed SD configuration out of chat, screenshots, command output and Git. Complete the device checks at the end and report which ones ran on physical hardware.

## 1. Prepare the host

Use a Linux machine with Docker Engine, the Docker Compose v2 plugin, Python 3, and outbound HTTPS to Google and YouTube. Give it enough temporary disk for concurrent track downloads and FFmpeg output. Point a DNS name at the host. The standalone example uses ports 80 and 443 for Caddy and automatic TLS. The R1 must be able to resolve the name, reach port 443 and validate the certificate with its clock set correctly.

```sh
git clone --branch experimental/r1-youtube-music https://github.com/vetka925/sonix-player-audiobooks.git
cd sonix-player-audiobooks/server/youtube-music
umask 077
export SONIX_STATE_DIR="$HOME/.local/state/sonix-youtube"
export SONIX_UID="$(id -u)" SONIX_GID="$(id -g)"
export MUSIC_HOST=music.example.net    # replace with your DNS name, without a scheme
install -d -m 700 "$SONIX_STATE_DIR"
docker compose build sonix-youtube
```

Run the following commands from this directory in the same shell. Re-export the variables when opening another shell. The container runs with the host user's numeric UID/GID and must be able to update both OAuth token files. Preserve the state volume across updates.

**Existing proxy on 80/443:** start only the application with `docker compose up -d sonix-youtube`. For containerized Caddy, attach Caddy and the application to a common Docker network (declare the external network in your deployment for persistence). Import [Caddyfile.fragment](Caddyfile.fragment) inside the public site's block. For a host-native proxy, publish the bridge on loopback, e.g. `127.0.0.1:18765:18765`, and use that loopback upstream. Strip the `/sonix-music` prefix, forward the Authorization header, allow long audio responses, and disable access logging for these routes. The supplied standalone Compose file handles the routing with a private Docker upstream.

## 2. Register the operator's Google client

1. In Google Cloud Console select or create a project and enable **YouTube Data API v3**.
2. Configure Google Auth Platform branding, support email, audience and developer contact. For a personal Gmail account use **External**. If the app's publishing status is **Testing**, add the exact Google account under **Audience → Test users**. Project ownership and OAuth test-user access are separate. Under Data Access configure the scopes used below if required by the console.
3. Under **Clients**, create an OAuth client of type **TVs and Limited Input devices** and download the JSON. Transfer it privately to the server, outside the checkout. The bridge needs just two fields in a flat JSON object. Run this with the path to the downloaded file:

```sh
python3 - /private/path/to/google-client.json <<'PY'
import json, os, sys
from pathlib import Path
src = json.loads(Path(sys.argv[1]).read_text())
client = src.get("installed", src)
state = Path(os.environ["SONIX_STATE_DIR"])
with (state / "client.json").open("x") as out:
    json.dump({k: client[k] for k in ("client_id", "client_secret")}, out)
PY

# Generate once. Reusing the key keeps existing cards paired.
python3 - <<'PY'
import os, secrets
from pathlib import Path
p = Path(os.environ["SONIX_STATE_DIR"]) / "device.key"
if not p.exists():
    with p.open("x") as out:
        out.write(secrets.token_hex(32) + "\n")
PY
```

On an existing installation retain its matching client and grants. The exclusive create above deliberately stops at an existing client file. [Google device authorization](https://developers.google.com/identity/protocols/oauth2/limited-input-device)

## 3. Pair both grants

Run one command at a time. While it polls, open its HTML file privately in a browser on the operator's computer. On a remote server, copy the named file from the private state directory via SCP in another terminal, then open the local copy. Remove that copy after consent. Each pairing link is short-lived.

```sh
docker compose run --rm --no-deps sonix-youtube python link_google.py --link-file /state/catalog-link.html
docker compose run --rm --no-deps sonix-youtube python link_google.py --playback --link-file /state/playback-link.html
```

The first grant requests `youtube.readonly` and writes `oauth.json`. The second requests `youtube` and writes `playback/oauth.json`. Google presents the second scope as permission to manage the YouTube account. The bridge uses it to read the music collections and obtain playback formats. Use the same account for both grants. The helper polls Google, refresh tokens stay on the server, and each pairing HTML is removed when the command finishes. A testing-mode app can require renewed consent after seven days.

## 4. Start the bridge and provision the card

```sh
docker compose up -d                 # standalone proxy deployment
docker compose ps
```

Check the public route from a machine on the player's network. Replace the hostname. An unauthenticated request should return **401**.

```sh
curl -sS -o /dev/null -w '%{http_code}\n' "https://$MUSIC_HOST/sonix-music/catalog/playlists"
```

Generate the device file privately on the server:

```sh
python3 - <<'PY'
import os, re
from pathlib import Path
state = Path(os.environ["SONIX_STATE_DIR"])
key = (state / "device.key").read_text().strip()
assert re.fullmatch(r"[0-9a-f]{64}", key)
url = "https://" + os.environ["MUSIC_HOST"] + "/sonix-music"
(state / "youtube-music.conf").write_text(f"{url}\n{key}\n")
PY
```

Copy `youtube-music.conf` via SSH/SCP to the computer with the SD card, then to the **root of the card**. Use the exact filename, two plain-text lines and no trailing slash on the URL. Possession of this card file grants access to the personal bridge. To rotate it, replace the server's device key, regenerate the card file and restart the bridge.

## 5. Install the matching R1 firmware

Download `r1.upt` from the [experimental release](https://github.com/vetka925/sonix-player-audiobooks/releases/tag/r1-youtube-music-v0.1.0-exp.1). Verify SHA-256 against the release notes. Confirm the device identifies itself as **HiBy R1**, charge it, and keep a known-good R1 image on the computer. Put the experimental `r1.upt` in the root of the card alongside `youtube-music.conf`. In Sonix choose **Settings → System → Update firmware → From SD card** and wait for the player to restart.

To build from source, follow the [device toolchain and packer instructions](../../README.md#building-for-the-device) from this branch. Run `make target` inside `sonix-player`, copy `sonix_player` and `sonix_launch` into `sonix-packer/`, put the operator's stock R1 image there as `r1_original.upt`, then run `bash sonix_firmware_packer.sh` in that directory. To roll back, repeat the SD update with the known-good R1 firmware.

## 6. Verify on the player

| Action | Expected result |
|---|---|
| Open Streaming → YouTube Music → Search, My playlists, Liked songs and Library songs | Search returns tracks and each personal section opens under the intended account. |
| Open a playlist and select a track | The first play downloads the track. The player shows elapsed time and Opus quality. Check seek, then select Next. |
| Put the display to sleep, let idle Wi-Fi park, wake and tap Liked songs once | The player waits for Wi-Fi and fills the list without a second tap. Scrolling the rows does not start playback. |
| Open a playlist or collection and choose Save list offline | All pages download into a folder under `YouTube Music/` on the card. Stop offline download finishes the current track before stopping. |
| Turn Wi-Fi off in Settings, open YouTube Music → Offline music, enter the saved folder, play a track and press Next | Both tracks play from local files. Restore the user's Wi-Fi setting afterwards. |

Streaming uses a separate 512 MiB cache under `.local/youtube/`. Offline files survive cache eviction and take space until the operator removes them. Offline music opens the file browser, which uses folder sorting. Re-running a save skips complete files with the same name. Treat downloads as snapshots. Removing a song on Google does not delete the local copy.

## If a check fails

| Symptom | Check |
|---|---|
| DNS/TLS error or 404 | Test the URL from the same Wi-Fi. Check the clock, trusted certificate chain, path-prefix stripping and the two-line card file. |
| 401 | Match the SD key with the server's device key and restart the bridge after rotation. Keep values out of logs. |
| 409 or consent expired | Pair the required grant again. Check that the consent account is an allowed test user and that the container can write the state mount. |
| Playlists work but likes, library songs or account quality fail | Check the second grant in `playback/oauth.json`. The Music TV browse/player APIs are unofficial and can change independently of the Data API. |
| 502/503 on a track | Try again after active downloads finish. Check disk space, FFmpeg, outbound access and the pinned extractor in the image. The bridge admits two concurrent audio requests. |
| Lower bitrate on a track | Formats depend on the account and source. A previously cached track keeps its existing encoding. MP3 re-encoding cannot improve the source quality. |

The release was exercised on a physical R1 with the two-grant bridge, including first-tap network reconnection and offline playback with the radio disabled. The extractor revision providing SABR transport is pinned in [requirements.txt](requirements.txt).
