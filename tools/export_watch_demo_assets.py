#!/usr/bin/env python3
"""Export website UI assets from the actual LovyanGFX firmware renderer.

Requires a native C++ compiler and Pillow; reads the installed PlatformIO
LovyanGFX dependency. No watch firmware or hardware is touched.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


def white_alpha(source: Image.Image) -> Image.Image:
    # Font sources are rendered white over black. Their coverage becomes alpha;
    # tinting is performed by the website without browser font substitutions.
    alpha = source.convert("RGB").getchannel("G")
    output = Image.new("RGBA", source.size, (255, 255, 255, 0))
    output.putalpha(alpha)
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "web/watch-demo/assets")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    library = ROOT / ".pio/libdeps/hacktor_watch_s3_lgfx/LovyanGFX/src"
    if not library.is_dir():
        raise SystemExit("LovyanGFX is missing; install the project's PlatformIO dependencies first.")

    with tempfile.TemporaryDirectory(prefix="hacktor-watch-demo-") as temporary:
        work = Path(temporary)
        (work / "SDL2").mkdir()
        (work / "SDL2/SDL.h").write_text("#pragma once\n#define SDL_h_\n")
        (work / "SDL2/SDL_main.h").write_text("#pragma once\n")
        executable = work / "export_watch_demo"
        command = [
            "c++", "-std=c++17", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Wno-vla-cxx-extension", "-I", str(work), "-I", str(ROOT / "include"),
            "-I", str(library), str(ROOT / "tools/export_watch_demo.cpp"),
            *[str(library / "lgfx/v1" / source) for source in
              ("LGFXBase.cpp", "lgfx_v1.cpp", "lgfx_fonts.cpp")],
            "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
            "-o", str(executable),
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([str(executable), str(work)], check=True, cwd=ROOT)
        transparent = {"hour-hand", "minute-hand", "shoe-icon",
                       "font2-atlas", "roboto-thin24-atlas", "digital-digits"}
        for path in sorted(work.glob("*.ppm")):
            with Image.open(path) as image:
                image = image.convert("RGB")
                if path.stem in transparent:
                    image = white_alpha(image)
                elif path.stem == "second-hand":
                    image = image.convert("RGBA")
                image.save(output / (path.stem + ".png"), optimize=True)
        fonts = json.loads((work / "native-fonts.json").read_text())

    metadata = {
        "version": 1,
        "display": {"width": 240, "height": 240, "center": [120, 120]},
        "demo": {"year": 2026, "month": 11, "day": 7, "weekday": 6,
                 "hour": 10, "minute": 8, "second": 30, "steps": 1957,
                 "battery": 79, "charging": False, "distanceKm": 1.5},
        "palette": ["#000000", "#000000", "#202020", "#303030", "#404040",
                    "#000000", "#000000", "#a0ebaa", "#ff0000", "#c80000",
                    "#008cff", "#ffc280", "#ffffff", "#c0c0c0", "#ff0000", "#ffffff"],
        "fonts": fonts,
        "hands": {
            "hour": {"image": "hour-hand.png", "pivot": [3, 100], "scale": [1, 1]},
            "minute": {"image": "minute-hand.png", "pivot": [3, 100], "scale": [1, 1]},
            "second": {"image": "second-hand.png", "pivot": [1, 100], "scale": [0.7, 1]},
        },
        "analog": {
            "dialImage": "analog-dial.png", "dateBox": [45, 56, 150, 28],
            "date": {"font": "font2", "position": [120, 74], "datum": "middle_center", "color": "#c0c0c0"},
            "battery": {"position": [145, 115], "size": [20, 12],
                        "outlineRect": [0, 0, 16, 12], "terminalRect": [16, 3, 3, 6],
                        "fillRect": [2, 2, 14, 8], "fillWidth": "floor(14 * percent / 100)",
                        "textPosition": [165, 113], "textDatum": "top_left", "font": "font2",
                        "textFormat": "percent left padded to 3 chars + %",
                        "color": "#c0c0c0", "fillColor": "#008cff", "lowColor": "#ff0000", "lowThreshold": 20},
            "steps": {"font": "font2", "rowY": 170, "shoeY": 165, "shoeSize": [17, 10],
                      "gap": 4, "datum": "middle_left", "color": "#c0c0c0", "thousandsSeparator": ","},
            "indicators": {"radius": 1200 / 10.4, "majorDotRadius": 4, "minorDotRadius": 1,
                           "hourMinuteColor": "#ff0000", "secondColor": "#c80000"},
            "centerCap": {"radius": 6, "color": "#c80000", "ringRadius": 2, "ringColor": "#000000"},
        },
        "digital": {
            "digits": {"image": "digital-digits.png", "cellWidth": 44, "cellHeight": 68,
                       "hourPosition": [28, 61], "minutePosition": [28, 133]},
            "rightX": 134, "white": "#ffffff", "mint": "#a0ebaa", "secondary": "#c0c0c0", "divider": "#404040",
            "dateBaseline": 49, "dayFont": "roboto-thin24", "monthFont": "font2", "monthGap": 4,
            "weekdayBaseline": 69, "dividerRows": [[134, 78, 79], [134, 128, 79], [134, 179, 67]],
            "stepsBaseline": 103, "stepsLabelBaseline": 121,
            "batteryBaseline": 154, "batteryLabelBaseline": 172,
            "distanceBaseline": 204, "distanceLabelBaseline": 221,
            "valueFont": "roboto-thin24", "labelFont": "font2", "textDatum": "baseline_left",
        },
        "fontDrawing": {
            "atlasDatum": "top_left", "cellOrigin": "glyph x/y from metadata",
            "drawCellAt": "cursorX - padding - glyph.leadingAdjustment, topY - padding",
            "stringStartAdjustment": "max(0, -firstGlyph.xOffset)",
            "cursorAdvance": "glyph.advance",
            "textWidth": "last cursor + max(last advance, last inkWidth + last xOffset)",
            "middleYOffset": "floor(font.height / 2)", "baselineYOffset": "font.baseline",
        },
        "references": {"analog": "analog-reference.png", "digital": "digital-reference.png"},
    }
    (output / "watchface-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Watch demo assets exported to {output}")
    for path in sorted(output.iterdir()):
        if path.suffix == ".png":
            with Image.open(path) as image:
                print(f"  {path.name}: {image.width}x{image.height} {image.mode}")


if __name__ == "__main__":
    main()
