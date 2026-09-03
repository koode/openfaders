import { channelMap } from "@/content/release";

/**
 * The hero visual: the five devices as macOS lists them, next to the faders they
 * land on. Drawn from the same channelMap the table uses, so the picture cannot
 * drift from the documented routing.
 *
 * Built from markup rather than a screenshot: it stays sharp at any size, follows
 * the theme, and needs no image asset.
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
                    // One row reads as selected. A neutral fill with an accent bar
                    // rather than a brand fill: the brand colour is near-solid in dark
                    // mode, which drops the channel label below readable contrast.
                    const isSelected = index === 2;
                    return (
                        <li
                            key={row.device}
                            className={
                                "relative flex items-center justify-between gap-4 py-2.5 pr-4 pl-4 " +
                                (isSelected ? "bg-secondary" : "")
                            }
                        >
                            {isSelected && (
                                <span className="bg-brand-solid absolute inset-y-0 left-0 w-0.5" />
                            )}
                            <span className="flex items-center gap-2.5">
                                <SpeakerIcon
                                    className={
                                        isSelected
                                            ? "text-fg-brand-primary size-4"
                                            : "text-fg-quaternary size-4"
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
                            <span className="text-tertiary shrink-0 text-xs tabular-nums">
                                ch {row.channels}
                            </span>
                        </li>
                    );
                })}
            </ul>

            <div className="border-secondary bg-secondary flex items-end justify-center gap-3 border-t px-4 py-5">
                {/* Fader heights are arbitrary positions, not measured levels. They
                    only show that each device arrives on a separate strip. */}
                {[46, 62, 78, 38, 54].map((height, index) => (
                    <Fader key={channelMap[index].device} height={height} label={channelMap[index].source} />
                ))}
            </div>
        </div>
    );
}

function Fader({ height, label }: { height: number; label: string }) {
    return (
        <div className="flex flex-col items-center gap-2">
            <div className="bg-quaternary relative h-24 w-1.5 rounded-full">
                <span
                    className="bg-brand-solid absolute bottom-0 w-full rounded-full"
                    style={{ height: `${height}%` }}
                />
                <span
                    className="border-secondary bg-primary absolute left-1/2 h-3.5 w-5 -translate-x-1/2 rounded-sm border shadow-sm"
                    style={{ bottom: `calc(${height}% - 7px)` }}
                />
            </div>
            <span className="text-quaternary max-w-12 truncate text-[10px]">{label}</span>
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
