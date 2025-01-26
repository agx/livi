# Light Video

Minimalist video player using GTK4, libadwaita and GStreamer. The main
purpose is to make playing hardware accelerated videos with e.g. hantro and
OpenGL simple.

It supports:

- Inhibiting suspend/idle when playing video
- Stopping video playback on (i.e. power button toggled) blank
- Registering as default video player in GNOME control center
- A [MPRIS interface][] to control the playback
- Resuming position on previously played videos
- Playing videos from popular online sites using [yt-dlp][] as "preprocessor".

![Playing video in landscape fullscreen mode](screenshots/landscape-fullscreen.png)

## Building

Flatpak build:

```sh
# Intial setup
flatpak install --user org.gnome.Sdk//master
flatpak install --user org.gnome.Platform//master
# Build
flatpak-builder --force-clean  --install --user _build/ org.sigxcpu.Livi.json
```

Regular build:

```sh
# Intial setup
apt build-dep .
# Build
meson setup . _build
meson compile -C _build
```

## Using

To play a video using the preprocessor use `-Y`:

```sh
livi -Y <url>
```

For this to work `yt-dlp` needs to be able to fetch a combined audio and
video stream. If this is possible for a given URL with the current `yt-dlp`
configuration can be checked with:

```sh
yt-dlp --list-formats <url>
```

At least one of the listed format there needs to contain a video *and*
audio stream as you'd otherwise get video playback without audio when
playing the URL in livi.

You can wiggle `yt-dlp` option to see if you can find a combined
format. Common options there are `--format-sort` and
`--extractor-args`. Once you've found suitable options you can put
them into `~/.config/yt-dlp.conf. E.g.

```sh
cat <<EOF > ~/.config/yt-dlp.conf
--format-sort=codec:h264,codec:vp8
--extractor-args=youtube:player-client=android
EOF
```

[MPRIS interface]: https://specifications.freedesktop.org/mpris-spec/
[yt-dlp]: https://github.com/yt-dlp/yt-dlp
