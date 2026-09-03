/**
 * The release the download buttons point at.
 *
 * Scripts/publish-r2.sh writes the same values to
 * https://downloads.openfaders.org/latest.json when it uploads a build. Keep this
 * file in step with that manifest; the site is prerendered, so it reads these
 * values at build time rather than fetching the manifest at runtime.
 */
export const release = {
    version: "0.1.0",
    minimumMacOS: "macOS 14",
    dmg: {
        url: "https://downloads.openfaders.org/v0.1.0/OpenFaders-0.1.0.dmg",
        size: "1.4 MB",
        sha256: "a67e0740bc40a63cbcdf6f9d74aff81fc00e97cf2fff3ec3f159fe851aa6a736",
    },
    pkg: {
        url: "https://downloads.openfaders.org/v0.1.0/OpenFaders-0.1.0.pkg",
        size: "1.4 MB",
        sha256: "5bd3ca17f426907f850012a6cc2b578249eee8f5b20cbdbd1401792a927c3dc9",
    },
} as const;

/** Each virtual device and the board source it feeds. */
export const channelMap = [
    { device: "OpenFaders System", channels: "1–2", source: "System" },
    { device: "OpenFaders Game", channels: "3–4", source: "Game" },
    { device: "OpenFaders Music", channels: "5–6", source: "Music" },
    { device: "OpenFaders Virtual A", channels: "7–8", source: "Virtual A" },
    { device: "OpenFaders Virtual B", channels: "9–10", source: "Virtual B" },
] as const;
