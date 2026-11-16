#!/bin/bash
#
# Exports the brand assets in docs/branding from the SVG sources in Design/:
#
#   Design/AppIcon.svg  -> openfaders-icon    the macOS app icon, with margin and shadow
#   Design/Logo.svg     -> openfaders-logo    the rounded tile, cropped to its edge
#   Design/Avatar.svg   -> openfaders-avatar  a full-bleed square for profile pictures
#
# Each one is copied as SVG and rendered as PNG in several sizes. Headless Chrome
# does the rendering because ImageMagick's own SVG renderer drops the drop-shadow
# filters; ImageMagick only scales the large render down to the smaller sizes.
#
# Run it again after any change to the sources and commit the result.
#
# Chrome binary (override via environment):
#   OPENFADERS_CHROME   /Applications/Google Chrome.app/Contents/MacOS/Google Chrome
#
# Prerequisites: Google Chrome and ImageMagick (`magick`). Does not require sudo.

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
chrome="${OPENFADERS_CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
output_dir="$repo_root/docs/branding"
sizes=(16 32 64 128 256 512 1024 2048)

if [[ ! -x "$chrome" ]]; then
    echo "error: Chrome not found at $chrome (set OPENFADERS_CHROME)" >&2
    exit 1
fi
if ! command -v magick >/dev/null; then
    echo "error: ImageMagick (magick) not found" >&2
    exit 1
fi

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

# Renders an SVG with a 1024 px canvas at the given scale factor to a PNG with a
# transparent background.
render() {
    local svg_path="$1" scale="$2" png_path="$3"
    "$chrome" --headless --disable-gpu --hide-scrollbars \
        --user-data-dir="$work_dir/chrome-profile" \
        --default-background-color=00000000 \
        --window-size=1024,1024 --force-device-scale-factor="$scale" \
        --screenshot="$png_path" "file://$svg_path" >/dev/null 2>&1
}

mkdir -p "$output_dir/svg" "$output_dir/png"

for variant in icon:AppIcon logo:Logo avatar:Avatar; do
    name="${variant%%:*}"
    source_svg="$repo_root/Design/${variant#*:}.svg"
    stem="openfaders-$name"

    cp "$source_svg" "$output_dir/svg/$stem.svg"

    render "$source_svg" 1 "$work_dir/$stem-1024.png"
    render "$source_svg" 2 "$work_dir/$stem-2048.png"

    # The avatar is opaque edge to edge, so it ships without an alpha channel.
    alpha_option=()
    if [[ "$name" == "avatar" ]]; then
        alpha_option=(-alpha off)
    fi

    for size in "${sizes[@]}"; do
        if [[ "$size" == 1024 || "$size" == 2048 ]]; then
            source_png="$work_dir/$stem-$size.png"
            resize_option=()
        else
            source_png="$work_dir/$stem-2048.png"
            resize_option=(-filter Lanczos -resize "${size}x${size}")
        fi
        magick "$source_png" ${resize_option[@]+"${resize_option[@]}"} \
            ${alpha_option[@]+"${alpha_option[@]}"} -strip \
            -define png:exclude-chunks=date,time \
            "$output_dir/png/$stem-$size.png"
    done

    echo "exported $stem"
done
