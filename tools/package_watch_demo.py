#!/usr/bin/env python3
"""Bundle the watch animation into one HTML file and a website-ready ZIP."""
import base64
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEMO = ROOT / "web" / "watch-demo"


def main():
    metadata = json.loads((DEMO / "assets/watchface-metadata.json").read_text())
    files = ["analog-dial.png", "hour-hand.png", "minute-hand.png", "second-hand.png",
             "shoe-icon.png", "font2-atlas.png", "roboto-thin24-atlas.png",
             "digital-digits.png", "watch-case-front.png"]
    images = {name: "data:image/png;base64," + base64.b64encode(
        (DEMO / "assets" / name).read_bytes()).decode("ascii") for name in files}
    data = json.dumps({"metadata": metadata, "images": images}, separators=(",", ":"))
    html = (DEMO / "index.html").read_text()
    html = html.replace('<link rel="stylesheet" href="watch-demo.css">',
                        "<style>" + (DEMO / "watch-demo.css").read_text() + "</style>")
    javascript = (DEMO / "watch-demo.js").read_text().replace("</script", "<\\/script")
    html = html.replace('<script src="watch-demo.js" defer></script>', "")
    # Place the program after the canvas so the standalone file also works
    # when opened directly from disk without a web server.
    program = "<script>window.WATCH_DEMO_ASSETS=" + data + ";</script>\n<script>" + javascript + "</script>"
    html = html.replace("</body>", program + "\n</body>")
    standalone = DEMO / "watch-demo.html"
    standalone.write_text(html)
    package = DEMO / "hacktor-watch-animation.zip"
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(DEMO.rglob("*")):
            if not path.is_file() or path == package or path.name.startswith(".") or ".sb-" in path.name:
                continue
            archive.write(path, "hacktor-watch-animation/" + str(path.relative_to(DEMO)))
    print(f"Standalone: {standalone} ({standalone.stat().st_size:,} bytes)")
    print(f"Website package: {package} ({package.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
