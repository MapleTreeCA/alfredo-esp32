#!/usr/bin/env python3

import argparse
import shutil
import subprocess
from pathlib import Path

from PIL import Image


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
SVG_DIR = SCRIPT_DIR / "alfred_svg"
SPEAKING_SUFFIX = "speaking"
MOUTH_SPEAKING_TRANSFORM = "translate(0 5) translate(64 92) scale(1 0.5) translate(-64 -92)"
CONTENT_PADDING_DIVISOR = 32


def find_default_component_dir() -> Path:
    candidates = []
    for root in (PROJECT_DIR / "managed_components", PROJECT_DIR / "components"):
        if not root.exists():
            continue
        for child in sorted(root.iterdir()):
            if not child.is_dir():
                continue
            if (child / "src" / "emoji").exists() and (child / "png").exists():
                candidates.append(child)
    if not candidates:
        raise FileNotFoundError("Could not locate the emoji/font component directory")
    return candidates[0]


DEFAULT_COMPONENT_DIR = find_default_component_dir()

FACE_TO_EMOTIONS = {
    "calm": ["neutral"],
    "smile": ["happy"],
    "worried": ["sad"],
    "sleeping": ["sleeping"],
    "thinking": ["thinking"],
    "listening": ["listening"],
    "noconnection": ["noconnection"],
    "grieved": ["grieved"],
}

EMOTION_CODEPOINTS = {
    "neutral": 0x1F636,
    "happy": 0x1F642,
    "sad": 0x1F614,
    "thinking": 0x1F914,
    "sleeping": 0xE001,
    "listening": 0xE002,
    "noconnection": 0xE003,
    "grieved": 0xE004,
}

PNG_COLLECTIONS = {
    32: ["twemoji_32"],
    64: ["twemoji_64", "noto-emoji_64"],
    128: ["twemoji_128", "noto-emoji_128"],
}

STATIC_SIZES = (32, 64)


def ensure_svg_sources():
    missing = [face for face in FACE_TO_EMOTIONS if not (SVG_DIR / f"{face}.svg").exists()]
    if missing:
        missing_list = ", ".join(sorted(missing))
        raise FileNotFoundError(f"Missing Alfred SVG face files: {missing_list}")


def reset_dir(dir_path: Path):
    dir_path.mkdir(parents=True, exist_ok=True)
    for child in dir_path.iterdir():
        if child.is_file() or child.is_symlink():
            child.unlink()
        else:
            shutil.rmtree(child)


def render_svg(svg_path: Path, png_path: Path, size: int):
    png_path.parent.mkdir(parents=True, exist_ok=True)
    with png_path.open("wb") as handle:
        subprocess.run(
            ["rsvg-convert", "-w", str(size), "-h", str(size), str(svg_path)],
            check=True,
            stdout=handle,
        )


def render_svg_content(svg_content: str, png_path: Path, size: int):
    png_path.parent.mkdir(parents=True, exist_ok=True)
    with png_path.open("wb") as handle:
        subprocess.run(
            ["rsvg-convert", "-w", str(size), "-h", str(size)],
            check=True,
            input=svg_content.encode("utf-8"),
            stdout=handle,
        )


def union_alpha_bbox(images: list[Image.Image]):
    bbox = None
    for image in images:
        alpha_bbox = image.getchannel("A").getbbox()
        if alpha_bbox is None:
            continue
        if bbox is None:
            bbox = alpha_bbox
            continue
        bbox = (
            min(bbox[0], alpha_bbox[0]),
            min(bbox[1], alpha_bbox[1]),
            max(bbox[2], alpha_bbox[2]),
            max(bbox[3], alpha_bbox[3]),
        )
    return bbox


def normalize_face_variants(image_paths: list[Path], size: int):
    images = [Image.open(path).convert("RGBA") for path in image_paths]
    bbox = union_alpha_bbox(images)
    if bbox is None:
        return

    padding = max(1, size // CONTENT_PADDING_DIVISOR)
    target_size = size - padding * 2
    resampling = getattr(Image, "Resampling", Image).LANCZOS

    for path, image in zip(image_paths, images):
        cropped = image.crop(bbox)
        width, height = cropped.size
        if width == 0 or height == 0:
            continue

        scale = min(target_size / width, target_size / height)
        resized = cropped.resize(
            (max(1, round(width * scale)), max(1, round(height * scale))),
            resample=resampling,
        )

        canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        paste_x = (size - resized.width) // 2
        paste_y = (size - resized.height) // 2
        canvas.paste(resized, (paste_x, paste_y), resized)
        canvas.save(path)


def build_speaking_svg(svg_path: Path) -> str:
    svg_content = svg_path.read_text(encoding="utf-8")
    marker = '<g class="mouth">'
    replacement = f'<g class="mouth" transform="{MOUTH_SPEAKING_TRANSFORM}">'
    if marker not in svg_content:
        return svg_content
    return svg_content.replace(marker, replacement, 1)


def rgb565a8_bytes(image: Image.Image) -> bytes:
    rgba = image.convert("RGBA")
    color_plane = bytearray()
    alpha_plane = bytearray()

    for red, green, blue, alpha in rgba.getdata():
        rgb565 = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        color_plane.append(rgb565 & 0xFF)
        color_plane.append((rgb565 >> 8) & 0xFF)
        alpha_plane.append(alpha)

    return bytes(color_plane + alpha_plane)


def format_c_array(data: bytes) -> str:
    lines = []
    row = []
    for index, value in enumerate(data, start=1):
        row.append(f"0x{value:02x}")
        if index % 16 == 0:
            lines.append("    " + ",".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ",".join(row) + ",")
    return "\n".join(lines)


def write_lvgl_image_c(png_path: Path, out_path: Path, symbol: str):
    image = Image.open(png_path)
    width, height = image.size
    data = rgb565a8_bytes(image)
    array_name = f"{symbol}_map"

    content = f"""
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_{symbol.upper()}
#define LV_ATTRIBUTE_{symbol.upper()}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_{symbol.upper()}
uint8_t {array_name}[] = {{
{format_c_array(data)}
}};

const lv_image_dsc_t {symbol} = {{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565A8,
  .header.flags = 0,
  .header.w = {width},
  .header.h = {height},
  .header.stride = {width * 2},
  .data_size = sizeof({array_name}),
  .data = {array_name},
}};
""".lstrip()

    out_path.write_text(content, encoding="utf-8")


def build_font_source(size: int) -> str:
    symbol_by_face = {face: f"alfred_face_{face}_{size}" for face in FACE_TO_EMOTIONS}
    externs = "\n".join(
        f'extern const lv_image_dsc_t {symbol}; // {face}'
        for face, symbol in symbol_by_face.items()
    )

    table_entries = []
    for emotion, codepoint in EMOTION_CODEPOINTS.items():
        face = next(face_name for face_name, emotions in FACE_TO_EMOTIONS.items() if emotion in emotions)
        symbol = symbol_by_face[face]
        table_entries.append(f"        {{ &{symbol}, 0x{codepoint:04x} }}, // {emotion}")

    table = "\n".join(table_entries)

    return f"""
#include "lvgl.h"

{externs}

typedef struct emoji_{size} {{
    const lv_image_dsc_t* emoji;
    uint32_t unicode;
}} emoji_{size}_t;

static const void* get_imgfont_path(const lv_font_t * font, uint32_t unicode, uint32_t unicode_next, int32_t * offset_y, void * user_data) {{
    static const emoji_{size}_t emoji_{size}_table[] = {{
{table}
    }};

    (void)font;
    (void)unicode_next;
    (void)offset_y;
    (void)user_data;

    for (size_t i = 0; i < sizeof(emoji_{size}_table) / sizeof(emoji_{size}_table[0]); i++) {{
        if (emoji_{size}_table[i].unicode == unicode) {{
            return emoji_{size}_table[i].emoji;
        }}
    }}
    return NULL;
}}

const lv_font_t* font_emoji_{size}_init(void) {{
    static lv_font_t* font = NULL;
    if (font == NULL) {{
        font = lv_imgfont_create({size}, get_imgfont_path, NULL);
        if (font == NULL) {{
            LV_LOG_ERROR("Failed to allocate memory for emoji font");
            return NULL;
        }}
        font->base_line = 0;
        font->fallback = NULL;
    }}
    return font;
}}
""".lstrip()


def write_font_sources(src_dir: Path):
    for size in STATIC_SIZES:
        target = src_dir / f"font_emoji_{size}.c"
        target.write_text(build_font_source(size), encoding="utf-8")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate Alfred emoji assets into the emoji/font component directory."
    )
    parser.add_argument(
        "--component-dir",
        default=str(DEFAULT_COMPONENT_DIR),
        help="Path to the target emoji/font component directory",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    component_dir = Path(args.component_dir).resolve()
    build_dir = component_dir / "build" / "alfred_faces"
    src_dir = component_dir / "src"
    emoji_src_dir = src_dir / "emoji"
    png_dir = component_dir / "png"

    ensure_svg_sources()
    if not component_dir.exists():
        raise FileNotFoundError(f"Target component directory does not exist: {component_dir}")

    reset_dir(emoji_src_dir)
    build_dir.mkdir(parents=True, exist_ok=True)

    for size, collection_names in PNG_COLLECTIONS.items():
        face_render_dir = build_dir / f"png_{size}"
        reset_dir(face_render_dir)

        for face in FACE_TO_EMOTIONS:
            svg_path = SVG_DIR / f"{face}.svg"
            base_png = face_render_dir / f"{face}.png"
            speaking_png = face_render_dir / f"{face}_{SPEAKING_SUFFIX}.png"
            render_svg(svg_path, base_png, size)
            render_svg_content(
                build_speaking_svg(svg_path),
                speaking_png,
                size,
            )
            normalize_face_variants([base_png, speaking_png], size)

        for collection_name in collection_names:
            collection_dir = png_dir / collection_name
            reset_dir(collection_dir)
            for face, emotions in FACE_TO_EMOTIONS.items():
                source_png = face_render_dir / f"{face}.png"
                speaking_png = face_render_dir / f"{face}_{SPEAKING_SUFFIX}.png"
                for emotion in emotions:
                    shutil.copyfile(source_png, collection_dir / f"{emotion}.png")
                    shutil.copyfile(
                        speaking_png,
                        collection_dir / f"{emotion}_{SPEAKING_SUFFIX}.png",
                    )

        if size in STATIC_SIZES:
            for face in FACE_TO_EMOTIONS:
                symbol = f"alfred_face_{face}_{size}"
                write_lvgl_image_c(
                    face_render_dir / f"{face}.png",
                    emoji_src_dir / f"{symbol}.c",
                    symbol,
                )

    write_font_sources(src_dir)
    print(f"Generated Alfred emoji assets for component: {component_dir}")


if __name__ == "__main__":
    main()
