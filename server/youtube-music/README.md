# Personal YouTube Music playback

The catalog uses the YouTube Data API with its read-only grant. For account
audio, mount the TV client's client.json and a separately authorized
playback/oauth.json under the private state directory. The latter uses the
YouTube OAuth scope. Keep both grants outside the image and source tree.

For the same track and access token, the WEB_REMIX player request returned
400 INVALID_ARGUMENT, while TVHTML5 returned OK and listed account formats.
The TV response supplied serverAbrStreamingUrl and no per-format media URLs.
The Docker image pins the SABR implementation from
[yt-dlp PR 13515](https://github.com/yt-dlp/yt-dlp/pull/13515).

The bridge prefers Opus format 774 and falls back to the best available Opus.
FFmpeg copies the packets into Ogg; the bridge adds metadata and artwork.
The R1 reuses files already cached on its card. To retest quality for a cached
track, pause playback and move that track's file out of the cache before
selecting it again.
