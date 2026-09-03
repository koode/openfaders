#!/bin/bash
#
# Uploads the built installers to the public R2 bucket and refreshes latest.json.
#
# GitHub Releases stays the versioned record, but the repository is private, so its
# download URLs need a token. The website links to R2 instead, which is public and
# has no egress cost.
#
# Objects are written under a per-version prefix and never overwritten, so a URL
# that has been published stays byte-identical forever. Only latest.json moves.
#
# Overrides via environment:
#   OPENFADERS_R2_BUCKET     openfaders-releases
#   CLOUDFLARE_ACCOUNT_ID    the Koode account
#
# Prerequisites: an authenticated wrangler (`npx wrangler login`) and the built
# installers in build/dist/. Run Scripts/build-release.sh and Scripts/build-dmg.sh
# first. Does not require sudo.

set -euo pipefail

readonly project_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$project_root"

readonly bucket="${OPENFADERS_R2_BUCKET:-openfaders-releases}"
readonly account_id="${CLOUDFLARE_ACCOUNT_ID:-415b003a8517dd18a1898b189116ce56}"
readonly public_base="https://downloads.openfaders.org"
readonly dist_dir="$project_root/build/dist"

# Read the version from the built app rather than project.yml, so what is published
# always matches what was actually built and notarized.
readonly app_bundle="$project_root/build/Release/OpenFaders.app"
if [[ ! -d "$app_bundle" ]]; then
    echo "error: $app_bundle not found. Run Scripts/build-release.sh first." >&2
    exit 1
fi
readonly version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
    "$app_bundle/Contents/Info.plist")"

readonly dmg="$dist_dir/OpenFaders-$version.dmg"
readonly pkg="$dist_dir/OpenFaders-$version.pkg"
for file in "$dmg" "$pkg"; do
    if [[ ! -f "$file" ]]; then
        echo "error: $file not found. Run Scripts/build-release.sh and Scripts/build-dmg.sh first." >&2
        exit 1
    fi
done

# Refuse to publish a build Apple has not notarized. Without a stapled ticket every
# download is met with a Gatekeeper block, and the point of the R2 mirror is a
# download that just works.
for file in "$dmg" "$pkg"; do
    if ! xcrun stapler validate "$file" >/dev/null 2>&1; then
        echo "error: $(basename "$file") carries no notarization ticket." >&2
        echo "       Rebuild with OPENFADERS_NOTARY_PROFILE set." >&2
        exit 1
    fi
done

upload() {
    local file="$1" key="$2" content_type="$3"
    echo "==> $key"
    CLOUDFLARE_ACCOUNT_ID="$account_id" npx --yes wrangler@latest r2 object put \
        "$bucket/$key" --file="$file" --content-type="$content_type" --remote >/dev/null
}

echo "==> Publishing $version to $bucket"
upload "$dmg" "v$version/$(basename "$dmg")" "application/x-apple-diskimage"
upload "$pkg" "v$version/$(basename "$pkg")" "application/vnd.apple.installer+xml"

# The manifest the website reads, so a release does not require a site rebuild.
readonly manifest="$dist_dir/latest.json"
cat > "$manifest" <<JSON
{
  "version": "$version",
  "publishedAt": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "dmg": {
    "url": "$public_base/v$version/$(basename "$dmg")",
    "size": $(stat -f%z "$dmg"),
    "sha256": "$(shasum -a 256 "$dmg" | cut -d' ' -f1)"
  },
  "pkg": {
    "url": "$public_base/v$version/$(basename "$pkg")",
    "size": $(stat -f%z "$pkg"),
    "sha256": "$(shasum -a 256 "$pkg" | cut -d' ' -f1)"
  }
}
JSON
# no-store: this is the one object that changes meaning between releases, so a
# cached copy would keep pointing at the previous version.
CLOUDFLARE_ACCOUNT_ID="$account_id" npx --yes wrangler@latest r2 object put \
    "$bucket/latest.json" --file="$manifest" --content-type="application/json" \
    --cache-control="no-store" --remote >/dev/null
echo "==> latest.json"

echo ""
echo "Done:"
echo "  $public_base/v$version/$(basename "$dmg")"
echo "  $public_base/v$version/$(basename "$pkg")"
echo "  $public_base/latest.json"
