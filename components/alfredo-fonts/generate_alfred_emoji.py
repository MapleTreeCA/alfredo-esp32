#!/usr/bin/env python3

import shutil
import subprocess
from pathlib import Path

from PIL import Image


COMPONENT_DIR = Path(__file__).resolve().parent
SVG_DIR = COMPONENT_DIR / "svg" / "alfred"
BUILD_DIR = COMPONENT_DIR / "tmp" / "alfred_faces"
SRC_DIR = COMPONENT_DIR / "src"
EMOJI_SRC_DIR = SRC_DIR / "emoji"
PNG_DIR = COMPONENT_DIR / "png"

FACE_TO_EMOTIONS = {
    "calm": ["neutral", "cool", "confident"],
    "smile": ["happy"],
    "laugh": ["laughing", "funny"],
    "worried": ["sad", "crying", "confused"],
    "angry": ["angry"],
    "shy": ["loving", "embarrassed", "kissy"],
    "surprised": ["surprised", "shocked"],
    "wink": ["winking", "silly"],
    "sleepy": ["relaxed", "sleepy"],
    "talk": ["delicious"],
    "sleeping": ["sleeping"],
    "sleeping0": ["sleeping0"],
    "sleeping1": ["sleeping1"],
    "sleeping2": ["sleeping2"],
    "sleeping3": ["sleeping3"],
    "thinking": ["thinking"],
    "listening": ["listening"],
    "listening1": ["listening1"],
    "listening2": ["listening2"],
    "listening3": ["listening3"],
    "noconnection": ["noconnection"],
    "grieved": ["grieved"],
    "wakeup1": ["wakeup1"],
    "wakeup2": ["wakeup2"],
}

EMOTION_CODEPOINTS = {
    "neutral": 0x1F636,
    "happy": 0x1F642,
    "laughing": 0x1F606,
    "funny": 0x1F602,
    "sad": 0x1F614,
    "angry": 0x1F620,
    "crying": 0x1F62D,
    "loving": 0x1F60D,
    "embarrassed": 0x1F633,
    "surprised": 0x1F62F,
    "shocked": 0x1F631,
    "thinking": 0x1F914,
    "winking": 0x1F609,
    "cool": 0x1F60E,
    "relaxed": 0x1F60C,
    "delicious": 0x1F924,
    "kissy": 0x1F618,
    "confident": 0x1F60F,
    "sleepy": 0x1F634,
    "silly": 0x1F61C,
    "confused": 0x1F644,
    "wakeup1": 0x1F971,
    "wakeup2": 0x1F604,
    "sleeping": 0x1F4A4,
    "sleeping0": 0x1F550,
    "sleeping1": 0x1F551,
    "sleeping2": 0x1F552,
    "sleeping3": 0x1F553,
    "listening": 0x1F3A7,
    "listening1": 0x1F554,
    "listening2": 0x1F555,
    "listening3": 0x1F556,
    "noconnection": 0x1F4F5,
    "grieved": 0x1F622,
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


def write_font_sources():
    for size in STATIC_SIZES:
        target = SRC_DIR / f"font_emoji_{size}.c"
        target.write_text(build_font_source(size), encoding="utf-8")


def main():
    ensure_svg_sources()

    reset_dir(EMOJI_SRC_DIR)
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    for size, collection_names in PNG_COLLECTIONS.items():
        face_render_dir = BUILD_DIR / f"png_{size}"
        reset_dir(face_render_dir)

        for face in FACE_TO_EMOTIONS:
            render_svg(SVG_DIR / f"{face}.svg", face_render_dir / f"{face}.png", size)

        for collection_name in collection_names:
            collection_dir = PNG_DIR / collection_name
            reset_dir(collection_dir)
            talk_png = face_render_dir / "talk.png"
            for face, emotions in FACE_TO_EMOTIONS.items():
                source_png = face_render_dir / f"{face}.png"
                for emotion in emotions:
                    shutil.copyfile(source_png, collection_dir / f"{emotion}.png")
                    # Keep *_speaking variants for runtime collections that use them.
                    # Use a dedicated talking mouth shape so speaking state is visible.
                    if collection_name == "twemoji_128":
                        shutil.copyfile(talk_png, collection_dir / f"{emotion}_speaking.png")

        if size in STATIC_SIZES:
            for face in FACE_TO_EMOTIONS:
                symbol = f"alfred_face_{face}_{size}"
                write_lvgl_image_c(
                    face_render_dir / f"{face}.png",
                    EMOJI_SRC_DIR / f"{symbol}.c",
                    symbol,
                )

    write_font_sources()
    print("Generated Alfred emoji assets for xiaozhi-fonts.")


if __name__ == "__main__":
    main()
