"use client";

import { useEffect, useState } from "react";
import { cx } from "@/utils/cx";

/**
 * Light/dark switch.
 *
 * The stylesheet keys dark mode off a `.dark-mode` class rather than
 * prefers-color-scheme, so the class is the single source of truth. The inline
 * script in the root layout sets it before first paint; this reads back what that
 * decided instead of guessing, which keeps the button label right on load.
 */
export function ThemeToggle({ className }: { className?: string }) {
    const [isDark, setIsDark] = useState(false);
    const [mounted, setMounted] = useState(false);

    useEffect(() => {
        setIsDark(document.documentElement.classList.contains("dark-mode"));
        setMounted(true);
    }, []);

    const toggle = () => {
        const next = !isDark;
        setIsDark(next);
        document.documentElement.classList.toggle("dark-mode", next);
        try {
            localStorage.setItem("theme", next ? "dark" : "light");
        } catch {
            // Private browsing can refuse localStorage. The class still applies for
            // this page view, which is all the button promises.
        }
    };

    return (
        <button
            type="button"
            onClick={toggle}
            aria-label={isDark ? "Switch to light mode" : "Switch to dark mode"}
            className={cx(
                "text-fg-quaternary hover:bg-primary_hover hover:text-fg-secondary inline-flex size-10 items-center justify-center rounded-lg transition duration-100 ease-linear",
                className,
            )}
        >
            {/* Rendered only after mount: the icon depends on the class the inline
                script set, which the server cannot know. */}
            {mounted && (isDark ? <SunIcon /> : <MoonIcon />)}
        </button>
    );
}

function SunIcon() {
    return (
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="size-5" aria-hidden="true">
            <circle cx="12" cy="12" r="4" />
            <path
                strokeLinecap="round"
                d="M12 2v2m0 16v2M4.9 4.9l1.4 1.4m11.4 11.4l1.4 1.4M2 12h2m16 0h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"
            />
        </svg>
    );
}

function MoonIcon() {
    return (
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="size-5" aria-hidden="true">
            <path strokeLinecap="round" strokeLinejoin="round" d="M21 12.8A9 9 0 1111.2 3a7 7 0 009.8 9.8z" />
        </svg>
    );
}
