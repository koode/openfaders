import { Button } from "@/components/base/buttons/button";
import { Badge } from "@/components/base/badges/badges";
import { RoutingPanel } from "@/components/site/routing-panel";
import { ThemeToggle } from "@/components/site/theme-toggle";
import { channelMap, release } from "@/content/release";

// Prerendered at build time. Nothing on this page is per-request.
export const dynamic = "force-static";

export default function HomePage() {
    return (
        <div className="flex min-h-screen flex-col">
            <SiteHeader />
            <main className="flex-1">
                <Hero />
                <WhatItSolves />
                <ChannelMap />
                <Features />
                <Install />
            </main>
            <SiteFooter />
        </div>
    );
}

function SiteHeader() {
    return (
        <header className="border-secondary border-b">
            <nav className="mx-auto flex max-w-5xl items-center justify-between px-6 py-4">
                <a href="/" className="flex items-center gap-2.5">
                    <img src="/appicon.png" alt="" className="size-8 rounded-lg" />
                    <span className="text-primary text-md font-semibold">OpenFaders</span>
                </a>
                <div className="flex items-center gap-1">
                    <a
                        href="#install"
                        className="text-tertiary hover:text-secondary hidden rounded-lg px-3 py-2 text-sm font-semibold transition duration-100 ease-linear sm:block"
                    >
                        Install
                    </a>
                    <a
                        href="#channels"
                        className="text-tertiary hover:text-secondary hidden rounded-lg px-3 py-2 text-sm font-semibold transition duration-100 ease-linear sm:block"
                    >
                        Channels
                    </a>
                    <ThemeToggle />
                </div>
            </nav>
        </header>
    );
}

function Hero() {
    return (
        <section className="mx-auto max-w-5xl px-6 pt-16 pb-20 md:pt-24 md:pb-28">
            <div className="grid items-center gap-12 lg:grid-cols-[1.05fr_1fr] lg:gap-16">
                <div>
                    <Badge type="pill-color" color="brand" size="md">
                        Version {release.version}
                    </Badge>
                    <h1 className="text-primary mt-5 text-display-md font-semibold tracking-tight md:text-display-lg">
                        Every app on its own fader
                    </h1>
                    <p className="text-tertiary mt-5 text-lg">
                        OpenFaders adds five virtual audio devices to macOS and routes each one into
                        its own channel pair on the RODECaster Pro II. Send a game to one, music to
                        another, and mix them on the board.
                    </p>

                    <div className="mt-8 flex flex-col gap-3 sm:flex-row sm:items-center">
                        <Button size="xl" href={release.dmg.url}>
                            Download for macOS
                        </Button>
                        <Button size="xl" color="secondary" href={release.pkg.url}>
                            Installer package
                        </Button>
                    </div>
                    <p className="text-quaternary mt-4 text-sm">
                        {release.dmg.size} · {release.minimumMacOS} or later · Apple Silicon and
                        Intel · signed and notarized by Apple
                    </p>
                </div>

                <RoutingPanel />
            </div>
        </section>
    );
}

function WhatItSolves() {
    return (
        <section className="border-secondary bg-secondary border-y">
            <div className="mx-auto max-w-5xl px-6 py-16 md:py-20">
                <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                    macOS sends everything to one output
                </h2>
                <div className="text-tertiary mt-5 grid gap-5 text-md md:grid-cols-2 md:gap-10">
                    <p>
                        The RODECaster Pro II accepts ten channels of audio over USB, which the board
                        splits into five named sources. macOS has no way to decide which app goes to
                        which channel, so everything arrives on one pair and lands on one fader.
                    </p>
                    <p>
                        OpenFaders publishes five output devices instead. Pick one in an app, or in
                        Sound settings, and its audio arrives on the matching source. Then raise the
                        fader.
                    </p>
                </div>
            </div>
        </section>
    );
}

function ChannelMap() {
    return (
        <section id="channels" className="mx-auto max-w-5xl px-6 py-16 md:py-20">
            <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                Which device lands where
            </h2>
            <p className="text-tertiary mt-3 max-w-2xl text-md">
                Set the board to Multitrack USB mode, then assign the source to a fader.
            </p>

            <div className="border-secondary mt-8 overflow-hidden rounded-xl border">
                <table className="w-full text-left text-sm">
                    <thead className="bg-secondary text-quaternary">
                        <tr>
                            <th className="px-5 py-3 font-medium">macOS output device</th>
                            <th className="px-5 py-3 font-medium">USB channels</th>
                            <th className="px-5 py-3 font-medium">Board source</th>
                        </tr>
                    </thead>
                    <tbody>
                        {channelMap.map((row) => (
                            <tr key={row.device} className="border-secondary border-t">
                                <td className="text-primary px-5 py-3 font-medium">{row.device}</td>
                                <td className="text-tertiary px-5 py-3 tabular-nums">{row.channels}</td>
                                <td className="text-tertiary px-5 py-3">{row.source}</td>
                            </tr>
                        ))}
                    </tbody>
                </table>
            </div>

            <p className="text-quaternary mt-4 text-sm">
                Only System is monitored by default. To hear the other four, set USB 1 Input to
                Expanded on the board and assign each source to a fader.
            </p>
        </section>
    );
}

const features = [
    {
        title: "Five virtual devices",
        body: "System, Game, Music, Virtual A and Virtual B appear in Sound settings like any other output. Each one is stereo and runs at 48 kHz.",
    },
    {
        title: "No drift on long sessions",
        body: "Routing is hardware-timed. Adaptive drift correction locks each virtual clock to the board, so a stream that runs for hours stays in step. No resampling.",
    },
    {
        title: "Installs its own driver",
        body: "The app carries the audio driver and installs it after one standard macOS password prompt. There is no Terminal step and no separate installer to find.",
    },
    {
        title: "Menu bar, no Dock icon",
        body: "Routing state and an on/off switch sit in the menu bar. Settings covers active devices, hardware output, drift correction and opening at login.",
    },
    {
        title: "Written from scratch",
        body: "A Core Audio AudioServerPlugIn built against Apple's public APIs. It is a clean-room alternative to the vendor's virtual-channels software, not a fork of it.",
    },
    {
        title: "MIT licensed",
        body: "The driver, the routing engine, the menu bar app and a headless CLI are all open source.",
    },
];

function Features() {
    return (
        <section className="border-secondary bg-secondary border-y">
            <div className="mx-auto max-w-5xl px-6 py-16 md:py-20">
                <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                    What it does
                </h2>
                <div className="mt-10 grid gap-x-8 gap-y-10 sm:grid-cols-2 lg:grid-cols-3">
                    {features.map((feature) => (
                        <div key={feature.title}>
                            <h3 className="text-primary text-md font-semibold">{feature.title}</h3>
                            <p className="text-tertiary mt-2 text-sm">{feature.body}</p>
                        </div>
                    ))}
                </div>
            </div>
        </section>
    );
}

const steps = [
    {
        title: "Install the app",
        body: "Open the disk image and drag OpenFaders to your Applications folder.",
    },
    {
        title: "Let it install the driver",
        body: "On first launch it asks once, with the standard macOS password prompt. Installing the driver restarts the system audio server, which interrupts all audio on the Mac for about a second.",
    },
    {
        title: "Send an app to a device",
        body: "Pick an OpenFaders device as the output, then bring the matching source up on a fader on the board.",
    },
];

function Install() {
    return (
        <section id="install" className="mx-auto max-w-5xl px-6 py-16 md:py-20">
            <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                Three steps
            </h2>

            <ol className="mt-10 grid gap-8 md:grid-cols-3">
                {steps.map((step, index) => (
                    <li key={step.title}>
                        <span className="bg-brand-solid text-white flex size-8 items-center justify-center rounded-full text-sm font-semibold tabular-nums">
                            {index + 1}
                        </span>
                        <h3 className="text-primary mt-4 text-md font-semibold">{step.title}</h3>
                        <p className="text-tertiary mt-2 text-sm">{step.body}</p>
                    </li>
                ))}
            </ol>

            <div className="border-secondary mt-12 rounded-xl border p-6 md:p-8">
                <h3 className="text-primary text-md font-semibold">Before you start</h3>
                <ul className="text-tertiary mt-3 list-disc space-y-2 pl-5 text-sm">
                    <li>{release.minimumMacOS} or later, on Apple Silicon or Intel.</li>
                    <li>
                        A RODECaster Pro II set to Multitrack USB mode on the device itself. No
                        software can change that setting, and OpenFaders does not try to.
                    </li>
                    <li>
                        The first time it routes audio, macOS asks for microphone access. OpenFaders
                        captures what apps play into its own virtual devices so it can forward that
                        to the board. It records no microphone.
                    </li>
                </ul>

                <div className="mt-6 flex flex-col gap-3 sm:flex-row">
                    <Button size="lg" href={release.dmg.url}>
                        Download {release.version}
                    </Button>
                    <Button size="lg" color="secondary" href={release.pkg.url}>
                        Download the .pkg installer
                    </Button>
                </div>
            </div>
        </section>
    );
}

function SiteFooter() {
    return (
        <footer className="border-secondary border-t">
            <div className="mx-auto flex max-w-5xl flex-col gap-4 px-6 py-10 md:flex-row md:items-center md:justify-between">
                <p className="text-quaternary text-sm">
                    OpenFaders is MIT licensed. Version {release.version}.
                </p>
                <p className="text-quaternary max-w-md text-sm">
                    Not affiliated with or endorsed by RØDE. RØDE and RODECaster are trademarks of
                    their respective owner.
                </p>
            </div>
        </footer>
    );
}
