import { channelMap } from "@/content/release";

/**
 * The hero visual: the five outputs as they appear in the Mac's sound settings,
 * with the board source each one feeds.
 *
 * Drawn from the same channelMap the rest of the page uses, so the picture cannot
 * drift from the documented routing. It deliberately shows only what exists: the
 * outputs OpenFaders adds. It does not draw faders or meters, because the app has
 * neither — the faders are on the board.
 */
export function RoutingPanel() {
    return (
        <div className="border-secondary bg-primary overflow-hidden rounded-2xl border shadow-lg">
            <div className="border-secondary bg-secondary flex items-center gap-2 border-b px-4 py-3">
                <span className="flex gap-1.5">
                    <span className="size-2.5 rounded-full bg-[#ff5f57]" />
                    <span className="size-2.5 rounded-full bg-[#febc2e]" />
                    <span className="size-2.5 rounded-full bg-[#28c840]" />
                </span>
                <span className="text-tertiary ml-1 text-xs font-medium">Sound — Output</span>
            </div>

            <ul className="divide-secondary divide-y">
                {channelMap.map((row, index) => {
                    // One row reads as chosen, the way an output picker shows the
                    // current selection. A neutral fill with an accent bar: the brand
                    // colour is near-solid in dark mode and drops the label below
                    // readable contrast.
                    const isSelected = index === 2;
                    return (
                        <li
                            key={row.device}
                            className={
                                "relative flex items-center justify-between gap-4 px-4 py-3 " +
                                (isSelected ? "bg-secondary" : "")
                            }
                        >
                            {isSelected && <span className="bg-brand-solid absolute inset-y-0 left-0 w-0.5" />}
                            <span className="flex items-center gap-2.5">
                                <SpeakerIcon
                                    className={
                                        isSelected ? "text-fg-brand-primary size-4" : "text-fg-quaternary size-4"
                                    }
                                />
                                <span
                                    className={
                                        isSelected
                                            ? "text-brand-secondary text-sm font-semibold"
                                            : "text-secondary text-sm"
                                    }
                                >
                                    {row.device}
                                </span>
                            </span>
                            <span className="text-tertiary shrink-0 text-xs">{row.source}</span>
                        </li>
                    );
                })}
            </ul>

            <div className="border-secondary bg-secondary border-t px-4 py-3">
                <p className="text-quaternary text-xs">
                    Five outputs on the Mac, five sources on the board.
                </p>
            </div>
        </div>
    );
}

function SpeakerIcon({ className }: { className?: string }) {
    return (
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className={className} aria-hidden="true">
            <path strokeLinecap="round" strokeLinejoin="round" d="M11 5L6 9H2v6h4l5 4V5z" />
            <path strokeLinecap="round" d="M15.5 8.5a5 5 0 010 7M18.5 5.5a9 9 0 010 13" />
        </svg>
    );
}
