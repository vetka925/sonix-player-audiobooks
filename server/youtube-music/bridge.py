"""Personal Sonix bridge. Catalog requires device auth, streams use short-lived tickets."""
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time

import requests
from flask import Flask, Response, abort, request, send_file
from mutagen.flac import Picture
from mutagen.oggopus import OggOpus
from ytmusicapi import OAuthCredentials, YTMusic

app = Flask(__name__)
state = Path(os.environ["SONIX_YTM_STATE"])
key = (state / "device.key").read_text().strip()
if len(key) < 32:
    raise ValueError("Invalid device key")
audio_slots = threading.BoundedSemaphore(2)
library_lock = threading.Lock()
video_id = re.compile(r"[A-Za-z0-9_-]{11}")
playlist_id = re.compile(r"[A-Za-z0-9_-]{1,128}")


def signature(track, expires):
    return hmac.new(key.encode(), f"{track}/{expires}".encode(), hashlib.sha256).hexdigest()


def clean(value):
    return " ".join(str(value or "").split())[:180]


def songs(items):
    for row in items:
        track = row.get("videoId") or ""
        if video_id.fullmatch(track) and row.get("isAvailable", True):
            expires = int(time.time()) + 4 * 3600
            artists = ", ".join(clean(a.get("name")) for a in row.get("artists") or [])
            title = " — ".join(filter(None, [clean(row.get("title")), artists]))
            yield ("song", track, title, f"audio/{track}/{expires}/{signature(track, expires)}")


def official_playlists(kind, query, limit, client, token_file):
    token = json.loads(token_file.read_text())

    def refresh():
        response = requests.post("https://oauth2.googleapis.com/token", timeout=15, data={
            **client, "grant_type": "refresh_token", "refresh_token": token["refresh_token"]})
        if response.status_code == 400:
            abort(409, "Reconnect Google on Rabisu")
        response.raise_for_status()
        update = response.json()
        token.update(update)
        token["expires_at"] = time.time() + update["expires_in"]
        temporary = token_file.with_suffix(".refresh")
        with open(temporary, "w", opener=lambda path, flags: os.open(path, flags, 0o600)) as out:
            json.dump(token, out)
        temporary.replace(token_file)

    if token.get("expires_at", 0) < time.time() + 60:
        refresh()
    listing = kind == "playlists"
    endpoint = "playlists" if listing else "playlistItems"
    params = {"part": "snippet", **({"mine": "true"} if listing else {"playlistId": query})}
    rows = []
    while len(rows) < limit:
        params["maxResults"] = min(50, limit - len(rows))
        url = "https://www.googleapis.com/youtube/v3/" + endpoint
        def fetch():
            return requests.get(url, params=params, timeout=20,
                                headers={"Authorization": "Bearer " + token["access_token"]})
        response = fetch()
        if response.status_code == 401:
            refresh()
            response = fetch()
        if response.status_code == 404:
            abort(404)
        if not response.ok:
            abort(502, "YouTube catalog unavailable")
        page = response.json()
        for item in page.get("items", []):
            snippet = item["snippet"]
            if listing:
                rows.append(("playlist", item["id"], clean(snippet["title"]), "-"))
            else:
                if snippet["title"] in ("Deleted video", "Private video") and not snippet.get("videoOwnerChannelId"):
                    continue
                rows.extend(songs([{"videoId": snippet.get("resourceId", {}).get("videoId"),
                                    "title": snippet["title"],
                                    "artists": [{"name": snippet.get("videoOwnerChannelTitle", "")}]}]))
        if not page.get("nextPageToken"):
            break
        params["pageToken"] = page["nextPageToken"]
    return rows


def private_catalog(kind, query, limit):
    client_file = state / "client.json"
    token_file = state / "oauth.json"
    if not client_file.exists() or not token_file.exists():
        abort(409, "Link Google on Rabisu")
    with library_lock:
        client = json.loads(client_file.read_text())
        if kind in ("playlist", "playlists"):
            return official_playlists(kind, query, limit, client, token_file)
        api = YTMusic(str(token_file), oauth_credentials=OAuthCredentials(**client))
        if kind == "songs":
            return songs(api.get_library_songs(limit))
        if kind == "likes":
            return songs(api.get_liked_songs(limit)["tracks"])


@app.after_request
def private_response(response):
    response.headers["Cache-Control"] = "private, no-store"
    return response


@app.get("/catalog/<kind>")
def catalog(kind):
    if not hmac.compare_digest(request.headers.get("Authorization", ""), "Bearer " + key):
        abort(401)
    query = request.args.get("q", "")
    offset = request.args.get("offset", 0, type=int)
    if not 0 <= offset <= 10000 or len(query) > 300:
        abort(400)
    limit = offset + 41
    if kind == "search" and query.strip():
        rows = songs(YTMusic().search(query, filter="songs", limit=limit))
    elif kind in ("songs", "likes", "playlists") or (kind == "playlist" and playlist_id.fullmatch(query)):
        rows = private_catalog(kind, query, limit)
    else:
        abort(400)
    page = list(rows)[offset:limit]
    lines = ["\t".join(row) for row in page[:40]]
    if len(page) > 40:
        lines.append("more\t-\tMore…\t-")
    return Response("\n".join(lines), mimetype="text/plain")


@app.get("/audio/<track>/<int:expires>/<ticket>")
def audio(track, expires, ticket):
    if not video_id.fullmatch(track) or expires < time.time() or not hmac.compare_digest(signature(track, expires), ticket):
        abort(403)
    if not audio_slots.acquire(blocking=False):
        abort(503)
    # Stream from a pipe. The device can stop mid-track without leaving workers running.
    source = encoder = None

    def finish():
        for proc in (encoder, source):
            if proc and proc.poll() is None:
                proc.kill()
            if proc:
                proc.wait()
        audio_slots.release()

    try:
        source = subprocess.Popen([sys.executable, "-m", "yt_dlp", "--no-playlist", "--quiet",
                                   "--socket-timeout", "12", "--retries", "1", "-f", "bestaudio/best",
                                   "-o", "-", "https://music.youtube.com/watch?v=" + track],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        encoder = subprocess.Popen(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", "pipe:0",
                                    "-vn", "-ac", "2", "-ar", "44100", "-codec:a", "libmp3lame",
                                    "-b:a", "160k", "-f", "mp3", "pipe:1"], stdin=source.stdout,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        source.stdout.close()
        first = encoder.stdout.read(4096)
        if not first:
            abort(502)
    except BaseException:
        finish()
        raise

    def chunks():
        yield first
        yield from iter(lambda: encoder.stdout.read(16384), b"")

    response = Response(chunks(), mimetype="audio/mpeg")
    response.call_on_close(finish)
    return response


@app.get("/track-ticket/<track>")
def track_ticket(track):
    if not hmac.compare_digest(request.headers.get("Authorization", ""), "Bearer " + key):
        abort(401)
    if not video_id.fullmatch(track):
        abort(400)
    expires = int(time.time()) + 900
    return Response(f"track/{track}/{expires}/{signature(track, expires)}", mimetype="text/plain")


def tag_opus(path, track, info):
    audio = OggOpus(path)
    api = YTMusic()
    watch = {}
    song = {}
    try:
        watch = api.get_watch_playlist(videoId=track, limit=1)
        song = next((t for t in watch["tracks"] if t.get("videoId") == track), {})
    except Exception:
        app.logger.warning("YouTube Music metadata unavailable")
    fields = {"title": song.get("title") or info.get("track") or info.get("title"),
              "artist": ", ".join(a["name"] for a in song.get("artists", []) if a.get("name")) or info.get("artist") or info.get("uploader"),
              "album": (song.get("album") or {}).get("name") or info.get("album")}
    for field, value in fields.items():
        if value:
            audio[field] = value
    if watch.get("lyrics"):
        try:
            lyrics = api.get_lyrics(watch["lyrics"])
            if lyrics and lyrics.get("lyrics"):
                audio["lyrics"] = lyrics["lyrics"]
        except Exception:
            app.logger.warning("Lyrics unavailable")
    images = song.get("thumbnail") or info.get("thumbnails") or []
    if isinstance(images, dict):
        images = images.get("thumbnails", [])
    if images:
        try:
            url = images[-1]["url"]
            image = requests.get(url, timeout=12)
            image.raise_for_status()
            jpeg = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", "pipe:0", "-vf",
                                   "scale=480:480:force_original_aspect_ratio=decrease", "-frames:v", "1",
                                   "-f", "mjpeg", "pipe:1"], input=image.content, capture_output=True,
                                  timeout=12, check=True).stdout
            picture = Picture()
            picture.type, picture.mime, picture.data = 3, "image/jpeg", jpeg
            audio["metadata_block_picture"] = base64.b64encode(picture.write()).decode("ascii")
        except (requests.RequestException, subprocess.SubprocessError, KeyError):
            app.logger.warning("Cover unavailable")
    audio.save()


@app.get("/track/<track>/<int:expires>/<ticket>")
def track_file(track, expires, ticket):
    if not video_id.fullmatch(track) or expires < time.time() or not hmac.compare_digest(signature(track, expires), ticket):
        abort(403)
    if not audio_slots.acquire(blocking=False):
        abort(503)
    try:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            output = Path(directory) / "track.opus"
            subprocess.run([sys.executable, "-m", "yt_dlp", "--no-playlist", "--quiet", "--write-info-json",
                            "--socket-timeout", "15", "--retries", "1", "--max-filesize", "240M",
                            "-f", "bestaudio[acodec=opus]", "-o", str(source),
                            "https://music.youtube.com/watch?v=" + track], capture_output=True, timeout=120, check=True)
            info = json.loads(source.with_suffix(".info.json").read_text())
            subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", str(source),
                            "-map", "0:a:0", "-c:a", "copy", str(output)], capture_output=True, timeout=40, check=True)
            tag_opus(output, track, info)
            size = output.stat().st_size
            response = send_file(output.open("rb"), mimetype="audio/ogg", conditional=False)
            response.content_length = size
            return response
    except (OSError, subprocess.SubprocessError):
        abort(502, "Original Opus unavailable")
    finally:
        audio_slots.release()
