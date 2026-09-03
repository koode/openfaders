import { defineConfig } from "vite";
import vinext from "vinext";
import { cloudflare } from "@cloudflare/vite-plugin";

/**
 * Cloudflare Workers with static assets.
 *
 * `prerender: { routes: "*" }` renders every page at build time, so the Worker
 * serves cached HTML instead of rendering per request. The site is a handful of
 * static pages, so nothing needs a server render.
 */
export default defineConfig({
    plugins: [
        vinext({ prerender: { routes: "*" } }),
        cloudflare({
            viteEnvironment: {
                name: "rsc",
                childEnvironments: ["ssr"],
            },
        }),
    ],
});
