# Hacktor Watch website animation

A front view of the actual watch case, showing the firmware's analog face,
a visible horizontal swipe, and the digital face. The yellow strap is drawn
in code to match the website. Display artwork and font atlases come from the
firmware's LovyanGFX renderer; the clock runs in the visitor's local time.

The 12-second sequence holds the analog face for 5 seconds, swipes left for
0.8 seconds, holds the digital face for 5.4 seconds, and swipes right for
0.8 seconds. Only the display moves during a swipe. The film returns cleanly
to its opening frame.

## Files

- `watch-demo.mp4`: H.264 video, 960 × 1200, 30 fps, silent, dark background.
- `watch-demo.webm`: VP9 alternative for browsers that prefer WebM.
- `poster.png`: still image for the video's loading state.
- `watch-demo.html`: self-contained live version; open it directly to preview.
- `index.html`, `watch-demo.js`, `watch-demo.css`, `assets/`: modular live version.
- `hacktor-watch-animation.zip`: all website files together.

The live version needs no framework, build step, CDN, external fonts, or network
service. It pauses when offscreen or in a hidden tab, respects reduced-motion
preferences, and includes an accessible play/pause button. Steps (1,957), battery
(79%), and estimated distance (1.5 km) are demonstration data, not live telemetry.

## Add the video

Copy the video files and poster to your site's assets directory. Set the
surrounding background to `#0f151a` to match the video.

```html
<video autoplay muted loop playsinline preload="metadata"
       poster="/assets/watch-animation/poster.png"
       width="960" height="1200"
       aria-label="Hacktor Watch analog and digital watchfaces">
  <source src="/assets/watch-animation/watch-demo.webm" type="video/webm">
  <source src="/assets/watch-animation/watch-demo.mp4" type="video/mp4">
</video>
```

The MP4 is suitable on its own when a single file is preferred. For visitors
who request reduced motion, show `poster.png` and a play button instead of
starting video automatically.

## Add the live version

Copy `watch-demo.html` to your website, then embed it:

```html
<iframe
  src="/assets/watch-animation/watch-demo.html?embed=1&background=transparent"
  title="Hacktor Watch live analog and digital watchfaces"
  loading="lazy"
  style="width:100%;max-width:600px;aspect-ratio:4/5;border:0;display:block"
></iframe>
```

The live animation can use a transparent background, making it suitable for
different website colors. Keep the play/pause control for accessibility; add
`&controls=0` only if your surrounding page supplies its own pause control.
Use `&mode=fixed` for the film's sample time and date, or `&autoplay=0` to start
paused. The normal default uses real local time.

For the modular version, serve this directory over HTTP:

```sh
python3 -m http.server 8000 --directory web/watch-demo
```

Open `http://localhost:8000/`. A page that loads the script directly can use:

```js
await window.watchDemo.ready;
window.watchDemo.pause();
window.watchDemo.play();
window.watchDemo.setSample({ steps: 3200, battery: 85, charging: false });
```

## Regenerate from the project root

Requires the installed PlatformIO LovyanGFX dependency, a C++ compiler, Python
with Pillow, Node.js with built-in WebSocket support, Chrome, and macOS Swift.
The export tools do not compile, flash, or modify the firmware.

```sh
python3 tools/export_watch_demo_assets.py
node tools/capture_watch_demo.mjs --root web/watch-demo \
  --output /tmp/hacktor-watch-demo-frames --duration 12 --fps 30 \
  --mp4 web/watch-demo/watch-demo.mp4 \
  --webm web/watch-demo/watch-demo.webm
cp /tmp/hacktor-watch-demo-frames/frame-00000.png web/watch-demo/poster.png
python3 tools/package_watch_demo.py
```

The native reference images in `assets/` are for checking watchface fidelity.
The case image is the unchanged transparent front CAD render from the
[Hacktor Watch hardware repository](https://raw.githubusercontent.com/dantudose/Hacktor-Watch-2.0/main/Images/front_view.png).
The display overlays that image inside a circular clip; no photograph was
retouched. Time in the recorded film advances while visible and is reset while
the analog face is hidden so the video loops cleanly. The live version keeps
actual time across face changes.
