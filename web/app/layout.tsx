import type { Metadata, Viewport } from "next";
// Fonts before the stylesheet: the @font-face rules must be emitted before
// Tailwind's layers, or preflight can order itself ahead of them.
import "@fontsource-variable/inter";
import "@/styles/globals.css";

export const metadata: Metadata = {
    metadataBase: new URL("https://openfaders.org"),
    title: {
        default: "OpenFaders — every app on its own fader",
        template: "%s — OpenFaders",
    },
    description:
        "OpenFaders adds five virtual audio devices to macOS and routes each one into its own channel pair on the RODECaster Pro II, so every app lands on its own fader.",
    openGraph: {
        title: "OpenFaders — every app on its own fader",
        description:
            "Five virtual audio devices for macOS, routed into the RODECaster Pro II's Multitrack USB return.",
        url: "https://openfaders.org",
        siteName: "OpenFaders",
        type: "website",
    },
    icons: { icon: "/appicon.png", apple: "/appicon.png" },
};

export const viewport: Viewport = {
    themeColor: [
        { media: "(prefers-color-scheme: light)", color: "#ffffff" },
        { media: "(prefers-color-scheme: dark)", color: "#0c111d" },
    ],
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
    return (
        <html lang="en">
            <head>
                {/*
                  Set the theme class before first paint. The stylesheet keys dark mode
                  off `.dark-mode` rather than prefers-color-scheme, so without this the
                  page paints light and then corrects itself.
                */}
                <script
                    dangerouslySetInnerHTML={{
                        __html: `(function(){try{var t=localStorage.getItem("theme");var d=t?t==="dark":window.matchMedia("(prefers-color-scheme: dark)").matches;document.documentElement.classList.toggle("dark-mode",d);}catch(e){}})();`,
                    }}
                />
            </head>
            <body className="bg-primary text-primary antialiased">{children}</body>
        </html>
    );
}
