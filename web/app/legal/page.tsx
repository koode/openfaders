import type { Metadata } from "next";
// Read from the repository root so the page cannot drift from the files that
// actually ship with the software.
import licenseText from "../../../LICENSE?raw";
import noticeText from "../../../NOTICE.md?raw";
import { SiteFooter, SiteHeader } from "@/components/site/chrome";

export const dynamic = "force-static";

export const metadata: Metadata = {
    title: "Legal",
    description:
        "License, trademark notice, disclaimer and privacy statement for OpenFaders.",
};

export default function LegalPage() {
    return (
        <div className="flex min-h-screen flex-col">
            <SiteHeader />
            <main className="mx-auto w-full max-w-3xl flex-1 px-6 py-16 md:py-20">
                <h1 className="text-primary text-display-sm font-semibold tracking-tight">Legal</h1>
                <p className="text-tertiary mt-4 text-md">
                    OpenFaders is free software, published by Koode. This page holds
                    the licence it ships under, the trademark notice, the disclaimer, and what the
                    software and this site do with your data.
                </p>

                <Section id="privacy" title="Privacy">
                    <p>
                        <strong className="text-secondary font-semibold">This site</strong> sets no
                        cookies, runs no analytics, and stores nothing about you. It is a set of
                        static files. Downloads are served from{" "}
                        <code className="text-secondary">downloads.openfaders.org</code>, which is
                        Cloudflare R2; Cloudflare logs requests as part of delivering them, and
                        OpenFaders neither receives nor keeps those logs.
                    </p>
                    <p>
                        <strong className="text-secondary font-semibold">The app</strong> makes one
                        kind of network request, and only that one: an update check. At most once a
                        day it fetches{" "}
                        <code className="text-secondary">
                            downloads.openfaders.org/latest.json
                        </code>
                        , a static file, to see whether a newer version exists. The request carries
                        no identifier, no query string and no body, and nothing about your Mac or
                        your setup is sent. Cloudflare sees the connection as it would any other
                        download. OpenFaders never installs an update on its own; it tells you and
                        stops there.
                    </p>
                    <p>
                        Turn it off in Settings ▸ Startup and the app makes no network connections at
                        all. There is no analytics, no telemetry, no crash reporting and no account.
                    </p>
                    <p>
                        Its only capability beyond audio is the macOS audio-input permission, which
                        it needs to read back what apps play into its own virtual devices so it can
                        forward that to your hardware. It does not record your microphone, and no
                        audio leaves your Mac.
                    </p>
                    <p>
                        There is no data to request, correct or delete, because none is collected.
                    </p>
                </Section>

                <Section id="disclaimer" title="Disclaimer">
                    <p>
                        OpenFaders installs a Core Audio driver into a system location and routes
                        audio to your hardware. Installing or updating the driver restarts the system
                        audio server, which briefly interrupts all audio on the machine.
                    </p>
                    <p>
                        The software is provided &ldquo;as is&rdquo;, without warranty of any kind,
                        express or implied, including but not limited to the warranties of
                        merchantability, fitness for a particular purpose and non-infringement. In no
                        event shall the authors or copyright holders be liable for any claim, damages
                        or other liability arising from, out of or in connection with the software or
                        its use. Use it at your own risk.
                    </p>
                </Section>

                <Section id="notice" title="Notice and trademarks">
                    <Prose text={stripHeading(noticeText)} />
                </Section>

                <Section id="license" title="Licence">
                    <p>OpenFaders is released under the MIT Licence.</p>
                    <pre className="border-secondary bg-secondary text-secondary mt-4 overflow-x-auto rounded-xl border p-5 text-xs leading-relaxed whitespace-pre-wrap">
                        {licenseText.trim()}
                    </pre>
                </Section>

                <Section id="contact" title="Contact">
                    <p>
                        Koode —{" "}
                        <a
                            href="mailto:hello@openfaders.org"
                            className="text-brand-secondary font-semibold underline underline-offset-2"
                        >
                            hello@openfaders.org
                        </a>
                    </p>
                </Section>
            </main>
            <SiteFooter />
        </div>
    );
}

function Section({ id, title, children }: { id: string; title: string; children: React.ReactNode }) {
    return (
        <section id={id} className="border-secondary mt-12 border-t pt-10">
            <h2 className="text-primary text-xl font-semibold">{title}</h2>
            <div className="text-tertiary mt-4 space-y-4 text-sm leading-relaxed">{children}</div>
        </section>
    );
}

/** Drops the file's own H1 so it does not fight the section heading. */
function stripHeading(markdown: string): string {
    return markdown.replace(/^#\s+.*\n+/, "");
}

/**
 * Renders the notice file. It is plain prose with `##` subheadings and blank-line
 * paragraphs, so a full Markdown parser would be more machinery than the content
 * needs. Anything richer belongs in a real renderer instead of here.
 */
function Prose({ text }: { text: string }) {
    const blocks = text.trim().split(/\n{2,}/);
    return (
        <>
            {blocks.map((block, index) => {
                const heading = block.match(/^##\s+(.*)$/);
                if (heading) {
                    return (
                        <h3 key={index} className="text-secondary pt-2 text-md font-semibold">
                            {heading[1]}
                        </h3>
                    );
                }
                return <p key={index}>{inlineCode(block.replace(/\n/g, " "))}</p>;
            })}
        </>
    );
}

/** Turns `backtick` spans into code elements. Without this they render literally. */
function inlineCode(text: string) {
    return text.split(/`([^`]+)`/g).map((part, index) =>
        index % 2 === 1 ? (
            <code key={index} className="text-secondary">
                {part}
            </code>
        ) : (
            part
        ),
    );
}
