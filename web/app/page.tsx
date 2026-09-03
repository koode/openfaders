import { Button } from "@/components/base/buttons/button";
import { Badge } from "@/components/base/badges/badges";
import { RoutingPanel } from "@/components/site/routing-panel";
import { SiteFooter, SiteHeader } from "@/components/site/chrome";
import { channelMap, release } from "@/content/release";

// Prerendered at build time. Nothing on this page is per-request.
export const dynamic = "force-static";

export default function HomePage() {
    return (
        <div className="flex min-h-screen flex-col">
            <SiteHeader />
            <main className="flex-1">
                <Hero />
                <Benefits />
                <Outputs />
                <Steps />
                <Download />
            </main>
            <SiteFooter />
        </div>
    );
}

function Hero() {
    return (
        <section className="mx-auto max-w-5xl px-6 pt-16 pb-20 md:pt-24 md:pb-28">
            <div className="grid items-center gap-12 lg:grid-cols-[1.05fr_1fr] lg:gap-16">
                <div>
                    <Badge type="pill-color" color="brand" size="md">
                        For the RODECaster Pro II
                    </Badge>
                    <h1 className="text-primary mt-5 text-display-md font-semibold tracking-tight md:text-display-lg">
                        Every app on its own fader
                    </h1>
                    <p className="text-tertiary mt-5 text-lg">
                        Your game, your music and your calls reach the board as one lump of sound.
                        OpenFaders adds five separate outputs to your Mac, so each app arrives on the
                        board as its own source and you can put it on its own fader.
                    </p>

                    <div className="mt-8 flex flex-col gap-3 sm:flex-row sm:items-center">
                        <Button size="xl" href={release.dmg.url}>
                            Download for Mac
                        </Button>
                        <Button size="xl" color="secondary" href="#how">
                            See how it works
                        </Button>
                    </div>
                    <p className="text-quaternary mt-4 text-sm">
                        Free · {release.dmg.size} · {release.minimumMacOS} or later
                    </p>
                </div>

                <RoutingPanel />
            </div>
        </section>
    );
}

const benefits = [
    {
        title: "Mix your apps live",
        body: "Once each app is on its own fader, the board does the rest. Pull the game down for a moment and put it back, without touching the music.",
    },
    {
        title: "Set it once",
        body: "Pick an OpenFaders output in an app and your Mac remembers. OpenFaders sits in the menu bar, with no Dock icon, and can start when you log in.",
    },
    {
        title: "Keeps in step, if you want",
        body: "A Mac and a mixer count time slightly differently, which can show up over a long session. Turn on Adaptive drift correction in Settings and OpenFaders follows the board's clock instead.",
    },
];

function Benefits() {
    return (
        <section className="border-secondary bg-secondary border-y">
            <div className="mx-auto max-w-5xl px-6 py-16 md:py-20">
                <div className="grid gap-10 md:grid-cols-3">
                    {benefits.map((benefit) => (
                        <div key={benefit.title}>
                            <h2 className="text-primary text-md font-semibold">{benefit.title}</h2>
                            <p className="text-tertiary mt-2 text-sm">{benefit.body}</p>
                        </div>
                    ))}
                </div>
            </div>
        </section>
    );
}

function Outputs() {
    return (
        <section id="channels" className="mx-auto max-w-5xl px-6 py-16 md:py-20">
            <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                Five outputs, five sources
            </h2>
            <p className="text-tertiary mt-3 max-w-2xl text-md">
                These appear in your Mac's sound settings, and in any app that lets you choose an
                output. Each one turns up on the board under a name you already know.
            </p>

            <div className="mt-8 grid gap-3 sm:grid-cols-2 lg:grid-cols-5">
                {channelMap.map((row) => (
                    <div key={row.device} className="border-secondary bg-primary rounded-xl border p-4">
                        <p className="text-primary text-sm font-semibold">
                            {row.device.replace("OpenFaders ", "")}
                        </p>
                        <p className="text-quaternary mt-1 text-xs">on the board: {row.source}</p>
                    </div>
                ))}
            </div>

            <p className="text-quaternary mt-6 max-w-2xl text-sm">
                Two things to do on the board the first time: set USB 1 Input to Expanded, and put
                each source on a spare fader. Out of the box only System is set up to be heard.
            </p>
        </section>
    );
}

const steps = [
    {
        title: "Install it",
        body: "Open the download and drag OpenFaders to your Applications folder.",
    },
    {
        title: "Say yes once",
        body: "The first time you open it, OpenFaders asks for your password so it can add the outputs. Your Mac's sound stops for about a second while it does.",
    },
    {
        title: "Choose an output",
        body: "In any app, pick an OpenFaders output instead of your usual one. Bring the matching source up on the board and you are mixing.",
    },
];

function Steps() {
    return (
        <section id="how" className="border-secondary bg-secondary border-y">
            <div className="mx-auto max-w-5xl px-6 py-16 md:py-20">
                <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                    Three steps to set up
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
            </div>
        </section>
    );
}

function Download() {
    return (
        <section id="install" className="mx-auto max-w-5xl px-6 py-16 md:py-24">
            <div className="border-secondary rounded-2xl border p-8 md:p-12">
                <h2 className="text-primary text-display-xs font-semibold md:text-display-sm">
                    Get OpenFaders
                </h2>
                <p className="text-tertiary mt-3 max-w-xl text-md">
                    Free and open source. No account and no sign-up.
                </p>

                <div className="mt-8 flex flex-col gap-3 sm:flex-row sm:items-center">
                    <Button size="xl" href={release.dmg.url}>
                        Download for Mac
                    </Button>
                    <Button size="xl" color="secondary" href={release.pkg.url}>
                        Installer version
                    </Button>
                </div>

                <dl className="text-quaternary mt-10 grid gap-6 text-sm sm:grid-cols-3">
                    <div>
                        <dt className="text-secondary font-semibold">You need</dt>
                        <dd className="mt-1">
                            A RODECaster Pro II, switched to Multitrack USB mode on the board itself.
                            No software can change that for you.
                        </dd>
                    </div>
                    <div>
                        <dt className="text-secondary font-semibold">Works on</dt>
                        <dd className="mt-1">
                            {release.minimumMacOS} or later, Apple Silicon or Intel. Version{" "}
                            {release.version}.
                        </dd>
                    </div>
                    <div>
                        <dt className="text-secondary font-semibold">Microphone prompt</dt>
                        <dd className="mt-1">
                            Say yes. OpenFaders needs it to pass your apps' sound through to the
                            board. It does not record you.
                        </dd>
                    </div>
                </dl>
            </div>
        </section>
    );
}
