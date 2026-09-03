"""Derives docs/demo.webp and packaging/store/store-preview.mp4 from a capture.

Both assets used to be produced by hand, with the recipe written down nowhere,
so regenerating them meant guessing at the previous sizes and frame rates. This
script is that recipe.

    python tools/make_demo_assets.py ime-recording/ime-recording.mp4

Two things it does not assume:

* **The achieved frame rate.** `capture_ime_recording.py` asks for 60 fps and
  does not get it -- compositing the HUD, pointer and click markers into every
  frame costs enough that a run lands nearer 24 fps. The container is no help:
  it reports `r_frame_rate` as a constant `2000/1` regardless. So the rate is
  measured from the frame timestamps, and the webp's frame delay is derived from
  that. Assume 60 and the animation plays at 2.5x speed.

* **That ffmpeg can write webp.** Several builds, Homebrew's included, ship
  without the encoder. The frames go through ffmpeg and the animation is
  assembled with Pillow, which is already a dependency via `wintegrate[video]`.

The store trailer is padded rather than cropped: the capture is 4:3 and the
Store wants 1080p, so 1024x768 scales to 1440x1080 and sits centred on a
1920x1080 black frame. Cropping to 16:9 would cut the taskbar, and the tray icon
there is one of the three things the listing says the trailer demonstrates.
No audio track: `tools/upload_store_submission.py` adds one, because the Store
requires trailers to have audio channels.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

#: The README shows the demo inline, so it is kept small and slow: one frame per
#: five, played back at a fifth of the capture rate, which is real time.
WEBP_DECIMATION = 5
WEBP_SIZE = (800, 600)
WEBP_QUALITY = 72

#: What the Store asks for, and what packaging/store/listing.*.md documents.
TRAILER_SIZE = (1920, 1080)
TRAILER_FPS = 60
TRAILER_CRF = 20


def _ffprobe_frame_times(path: pathlib.Path) -> list[float]:
    out = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "frame=pts_time",
            "-of",
            "csv=p=0",
            str(path),
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    return [float(x.rstrip(",")) for x in out if x.strip(",")]


def measure_fps(path: pathlib.Path) -> tuple[float, int, float]:
    """Frames per second actually achieved, from the timestamps.

    Returns (fps, frame_count, span_seconds). Never read this off
    `r_frame_rate`: the recorder writes a constant there.
    """
    times = _ffprobe_frame_times(path)
    if len(times) < 2:
        raise SystemExit(f"{path} has {len(times)} frames; nothing to derive")
    span = times[-1] - times[0]
    return len(times) / span, len(times), span


def build_webp(source: pathlib.Path, out: pathlib.Path, fps: float) -> None:
    from PIL import Image

    rate = fps / WEBP_DECIMATION
    delay_ms = round(1000 / rate)
    with tempfile.TemporaryDirectory() as tmp:
        frames_dir = pathlib.Path(tmp)
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-i",
                str(source),
                "-vf",
                f"select='not(mod(n\\,{WEBP_DECIMATION}))',"
                f"scale={WEBP_SIZE[0]}:{WEBP_SIZE[1]}:flags=lanczos",
                "-fps_mode",
                "passthrough",
                str(frames_dir / "f%05d.png"),
            ],
            check=True,
        )
        frames = sorted(frames_dir.glob("*.png"))
        if not frames:
            raise SystemExit("ffmpeg produced no frames")
        images = [Image.open(p).convert("RGB") for p in frames]
        out.parent.mkdir(parents=True, exist_ok=True)
        images[0].save(
            out,
            save_all=True,
            append_images=images[1:],
            duration=delay_ms,
            loop=0,
            quality=WEBP_QUALITY,
            method=4,
        )
    print(
        f"{out}: {len(frames)} frames, {delay_ms} ms each "
        f"-> {len(frames) * delay_ms / 1000:.1f}s at {rate:.3f} fps, "
        f"{out.stat().st_size / 1048576:.2f} MB"
    )


def build_trailer(source: pathlib.Path, out: pathlib.Path) -> None:
    width, height = TRAILER_SIZE
    scaled_w = round(height * 4 / 3)  # the capture is 4:3
    pad_x = (width - scaled_w) // 2
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-i",
            str(source),
            "-vf",
            f"scale={scaled_w}:{height}:flags=lanczos,"
            f"pad={width}:{height}:{pad_x}:0:color=black,fps={TRAILER_FPS}",
            "-c:v",
            "libx264",
            "-preset",
            "slow",
            "-crf",
            str(TRAILER_CRF),
            "-pix_fmt",
            "yuv420p",
            "-an",
            str(out),
        ],
        check=True,
    )
    print(f"{out}: {width}x{height} at {TRAILER_FPS} fps, {out.stat().st_size / 1048576:.2f} MB")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=pathlib.Path, help="ime-recording.mp4 from CI")
    parser.add_argument("--webp", type=pathlib.Path, default=pathlib.Path("docs/demo.webp"))
    parser.add_argument(
        "--trailer",
        type=pathlib.Path,
        default=pathlib.Path("packaging/store/store-preview.mp4"),
    )
    args = parser.parse_args()

    if not args.recording.is_file():
        raise SystemExit(f"no such recording: {args.recording}")
    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            raise SystemExit(f"{tool} not found on PATH")

    fps, frames, span = measure_fps(args.recording)
    print(f"{args.recording}: {frames} frames over {span:.1f}s = {fps:.3f} fps achieved")

    build_webp(args.recording, args.webp, fps)
    build_trailer(args.recording, args.trailer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
