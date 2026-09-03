import { release } from "@/content/release";
import { ThemeToggle } from "@/components/site/theme-toggle";

/** Shared across pages so the header and footer cannot drift between them. */
export function SiteHeader() {
    return (
        <header className="border-secondary border-b">
            <nav className="mx-auto flex max-w-5xl items-center justify-between px-6 py-4">
                <a href="/" className="flex items-center gap-2.5">
                    <img src="/appicon.png" alt="" className="size-8 rounded-lg" />
                    <span className="text-primary text-md font-semibold">OpenFaders</span>
                </a>
                <div className="flex items-center gap-1">
                    <a
                        href="/#install"
                        className="text-tertiary hover:text-secondary hidden rounded-lg px-3 py-2 text-sm font-semibold transition duration-100 ease-linear sm:block"
                    >
                        Install
                    </a>
                    <a
                        href="/#channels"
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

const footerLinks = [
    {
        heading: "Product",
        links: [
            { label: "Download", href: release.dmg.url },
            { label: "Installer package", href: release.pkg.url },
            { label: "Channel map", href: "/#channels" },
            { label: "Install steps", href: "/#install" },
        ],
    },
    {
        heading: "Legal",
        links: [
            { label: "Licence (MIT)", href: "/legal#license" },
            { label: "Notice and trademarks", href: "/legal#notice" },
            { label: "Disclaimer", href: "/legal#disclaimer" },
            { label: "Privacy", href: "/legal#privacy" },
        ],
    },
];

export function SiteFooter() {
    return (
        <footer className="border-secondary bg-secondary border-t">
            <div className="mx-auto max-w-5xl px-6 py-12">
                <div className="grid gap-10 md:grid-cols-[1.4fr_1fr_1fr]">
                    <div>
                        <div className="flex items-center gap-2.5">
                            <img src="/appicon.png" alt="" className="size-7 rounded-lg" />
                            <span className="text-primary text-sm font-semibold">OpenFaders</span>
                        </div>
                        <p className="text-tertiary mt-3 max-w-xs text-sm">
                            Five virtual audio devices for macOS, routed into the RODECaster Pro II
                            over its Multitrack USB return.
                        </p>
                        <p className="text-quaternary mt-4 text-sm">
                            <a
                                href="mailto:hello@openfaders.org"
                                className="hover:text-tertiary underline underline-offset-2"
                            >
                                hello@openfaders.org
                            </a>
                        </p>
                    </div>

                    {footerLinks.map((column) => (
                        <div key={column.heading}>
                            <h2 className="text-quaternary text-sm font-semibold">{column.heading}</h2>
                            <ul className="mt-3 space-y-2">
                                {column.links.map((link) => (
                                    <li key={link.label}>
                                        <a
                                            href={link.href}
                                            className="text-tertiary hover:text-secondary text-sm transition duration-100 ease-linear"
                                        >
                                            {link.label}
                                        </a>
                                    </li>
                                ))}
                            </ul>
                        </div>
                    ))}
                </div>

                <div className="border-secondary mt-10 space-y-3 border-t pt-8">
                    <p className="text-quaternary text-xs">
                        © 2026 Koode. OpenFaders is free software under the{" "}
                        <a href="/legal#license" className="hover:text-tertiary underline underline-offset-2">
                            MIT Licence
                        </a>
                        , provided without warranty of any kind. Version {release.version}.
                    </p>
                    <p className="text-quaternary text-xs">
                        Not affiliated with, endorsed by, or sponsored by RØDE Microphones /
                        Freedman Electronics. &ldquo;RØDE&rdquo; and &ldquo;RODECaster&rdquo; are
                        trademarks of their respective owner, used here only to describe hardware
                        compatibility.
                    </p>
                    <p className="text-quaternary text-xs">
                        OpenFaders installs a Core Audio driver into a system location and restarts
                        the system audio server. Use it at your own risk. See the{" "}
                        <a href="/legal#disclaimer" className="hover:text-tertiary underline underline-offset-2">
                            disclaimer
                        </a>
                        .
                    </p>
                </div>
            </div>
        </footer>
    );
}
