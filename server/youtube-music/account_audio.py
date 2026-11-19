"""Fetch original Opus using the TV OAuth grant. Only the player API gets the bearer."""
import fcntl
import json
import os
from pathlib import Path
import sys
import tempfile
import time

import requests
from yt_dlp import YoutubeDL
from yt_dlp.extractor.youtube import YoutubeIE as BaseYoutubeIE


def access_token(state):
    directory = state / "playback"
    with (directory / "refresh.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = directory / "oauth.json"
        token = json.loads(path.read_text())
        if token["expires_at"] < time.time() + 120:
            client = json.loads((state / "client.json").read_text())
            response = requests.post("https://oauth2.googleapis.com/token", data={
                **client, "grant_type": "refresh_token", "refresh_token": token["refresh_token"]},
                allow_redirects=False, timeout=15)
            response.raise_for_status()
            token.update(response.json())
            token["expires_at"] = time.time() + token["expires_in"]
            with tempfile.NamedTemporaryFile(mode="w", dir=directory, delete=False) as tmp:
                json.dump(token, tmp)
            os.replace(tmp.name, path)
        return token["access_token"]


class YoutubeIE(BaseYoutubeIE):
    def _call_api(self, ep, query, video_id, **kwargs):
        if ep == "player" and kwargs.get("default_client") == "tv":
            headers = dict(kwargs.get("headers") or {})
            headers["Authorization"] = "Bearer " + access_token(Path(os.environ["SONIX_YTM_STATE"]))
            kwargs.update(headers=headers, api_hostname="www.youtube.com")
        return super()._call_api(ep, query, video_id, **kwargs)


def download(track, path):
    options = {"quiet": True, "noprogress": True, "noplaylist": True,
               "extractor_args": {"youtube": {"player_client": ["tv"]}},
               "js_runtimes": {"node": {}}, "format": "774/bestaudio[acodec=opus]",
               "socket_timeout": 15, "retries": 1, "max_filesize": 240 * 1024**2,
               "outtmpl": str(path)}
    with YoutubeDL(options, auto_init=False) as downloader:
        downloader.add_info_extractor(YoutubeIE(downloader))
        result = downloader.extract_info("https://www.youtube.com/watch?v=" + track)
    metadata = {k: result.get(k) for k in ("title", "track", "artist", "album", "uploader", "thumbnails")}
    path.with_suffix(".info.json").write_text(json.dumps(metadata))


if __name__ == "__main__":
    download(sys.argv[1], Path(sys.argv[2]))
