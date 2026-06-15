#!/usr/bin/env python3

import argparse
import gzip
import shutil
import sys
import zipfile
from pathlib import Path


ITCH_MAX_FILES = 1000
ITCH_MAX_TOTAL_BYTES = 500 * 1000 * 1000
ITCH_MAX_SINGLE_FILE_BYTES = 200 * 1000 * 1000


def remove_tree_contents(path):
    if not path.exists():
        return
    for child in path.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def reject_unsafe_paths(build_dir, stage_dir, output_path):
    if stage_dir == build_dir:
        raise SystemExit("--stage-dir must not be the build directory")
    try:
        stage_dir.relative_to(build_dir)
    except ValueError:
        raise SystemExit("--stage-dir must be inside --build-dir") from None
    if stage_dir == stage_dir.parent:
        raise SystemExit("--stage-dir must not be a filesystem root")
    if stage_dir == Path.home():
        raise SystemExit("--stage-dir must not be the user home directory")
    if output_path == stage_dir or stage_dir in output_path.parents:
        raise SystemExit("--output must not be inside --stage-dir")


def find_single(path, pattern):
    matches = sorted(path.glob(pattern))
    if len(matches) != 1:
        raise SystemExit(f"expected exactly one {pattern} in {path}, found {len(matches)}")
    return matches[0]


def copy_and_gzip_data(src, dst):
    with src.open("rb") as input_file, dst.open("wb") as raw_output:
        with gzip.GzipFile(
            filename="",
            mode="wb",
            fileobj=raw_output,
            compresslevel=9,
            mtime=0,
        ) as output_file:
            shutil.copyfileobj(input_file, output_file)


def reject_stock_shell(index_path):
    text = index_path.read_text(encoding="utf-8", errors="replace")
    stock_markers = (
        "Emscripten-Generated Code",
        "emscripten_logo",
        "<textarea",
        "Resize canvas",
        "Lock/hide mouse pointer",
    )
    hits = [marker for marker in stock_markers if marker in text]
    if hits:
        raise SystemExit(
            "refusing to package the stock Emscripten shell; configure with "
            "-DFORGE_GPU_WEB_RELEASE=ON. Markers: " + ", ".join(hits)
        )


def check_limits(stage_dir):
    files = [path for path in stage_dir.rglob("*") if path.is_file()]
    if len(files) > ITCH_MAX_FILES:
        raise SystemExit(f"itch package has {len(files)} files, limit is {ITCH_MAX_FILES}")

    total = sum(path.stat().st_size for path in files)
    if total > ITCH_MAX_TOTAL_BYTES:
        raise SystemExit(f"itch package extracts to {total} bytes, limit is {ITCH_MAX_TOTAL_BYTES}")

    oversized = [
        (path, path.stat().st_size)
        for path in files
        if path.stat().st_size > ITCH_MAX_SINGLE_FILE_BYTES
    ]
    if oversized:
        details = ", ".join(f"{path.relative_to(stage_dir)}={size}" for path, size in oversized)
        raise SystemExit(f"itch package has files over {ITCH_MAX_SINGLE_FILE_BYTES} bytes: {details}")

    if not (stage_dir / "index.html").exists():
        raise SystemExit("itch package must contain top-level index.html")

    return files, total


def make_zip(stage_dir, output_path):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()
    with zipfile.ZipFile(
        output_path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for path in sorted(stage_dir.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(stage_dir).as_posix())


def main():
    parser = argparse.ArgumentParser(description="Package the Emscripten Forge GPU build for itch.io.")
    parser.add_argument(
        "--build-dir",
        required=True,
        type=Path,
        help="Emscripten build directory containing forge-gpu-demo.html/js/wasm/data",
    )
    parser.add_argument(
        "--stage-dir",
        required=True,
        type=Path,
        help="Temporary package staging directory",
    )
    parser.add_argument("--output", required=True, type=Path, help="Output ZIP path")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    stage_dir = args.stage_dir.resolve()
    output_path = args.output.resolve()
    reject_unsafe_paths(build_dir, stage_dir, output_path)

    html = find_single(build_dir, "*.html")
    js = build_dir / "forge-gpu-demo.js"
    wasm = build_dir / "forge-gpu-demo.wasm"
    data = build_dir / "forge-gpu-demo.data"
    for path in (js, wasm, data):
        if not path.exists():
            raise SystemExit(f"missing required browser artifact: {path}")

    stage_dir.mkdir(parents=True, exist_ok=True)
    remove_tree_contents(stage_dir)

    index = stage_dir / "index.html"
    shutil.copy2(html, index)
    reject_stock_shell(index)
    shutil.copy2(js, stage_dir / js.name)
    shutil.copy2(wasm, stage_dir / wasm.name)
    copy_and_gzip_data(data, stage_dir / data.name)

    files, total = check_limits(stage_dir)
    make_zip(stage_dir, output_path)

    largest_size, largest_name = max(
        (path.stat().st_size, path.relative_to(stage_dir).as_posix())
        for path in files
    )
    print(f"wrote {output_path}")
    print(f"stage: {stage_dir}")
    print(f"files: {len(files)}")
    print(f"total extracted bytes: {total}")
    print(f"largest file: {largest_name} ({largest_size} bytes)")


if __name__ == "__main__":
    sys.exit(main())
