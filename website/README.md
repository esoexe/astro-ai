# A.S.T.R.O AI website

The official site for A.S.T.R.O AI: plain static HTML, CSS and JS with no frameworks, no trackers and no external requests.
The deployable site is the **`dist/`** folder (about 27 MB including the downloads).

```
website/
├── dist/            ← DEPLOY THIS FOLDER (generated, don't edit by hand)
├── src/
│   ├── pages/         page bodies (*.html); shared header and footer are added by build.js
│   │   └── blog/      blog index + posts
│   └── static/        copied as-is: assets/css, assets/js, fonts, images, press kit, _headers, vercel.json
├── site.json          site URL, version, release date   ← EDIT url BEFORE DEPLOYING
├── build.js           builds src/ → dist/ (copies CLEAN downloads, writes checksums, sitemap, robots.txt)
├── icons.js           inline SVG icons used as {{icon:name}}
├── serve.js           local preview server (node serve.js 3000)
└── netlify.toml       tells Netlify to publish ./dist
```

Pages: Home · Features · Download · Docs · Changelog · Privacy · Press kit · Blog (3 posts) · 404.

## 1. Set your domain

Open `site.json` and replace the placeholder URL:

```json
"url": "https://astro-ai.example"
```

with your real address (for example `https://astro-ai.netlify.app` or your own domain). It's used for canonical links,
social-card previews (`og:image`), `sitemap.xml` and `robots.txt`. Then rebuild:

```bash
node build.js
```

The site must live at the **root** of its domain. The 404 page uses `/`-absolute links, which every host below supports.
If you use GitHub Pages, use a user/organisation site or a custom domain, not a `/repo-name/` project path.

## 2. Deploy `dist/` (pick one)

| Host | How |
|---|---|
| **Netlify** | Drag the `dist` folder onto <https://app.netlify.com/drop>. Or, with the CLI: `npx netlify-cli deploy --prod --dir dist` |
| **Cloudflare Pages** | `npx wrangler pages deploy dist --project-name astro-ai` (files must be under 25 MiB each; the largest is about 8 MB) |
| **Vercel** | `npx vercel deploy dist --prod` (`vercel.json` inside `dist/` turns on clean URLs and headers) |
| **GitHub Pages** | Push the *contents* of `dist/` to the branch or folder Pages serves (`.nojekyll` is already included) |

Security and cache headers come preconfigured: `_headers` for Netlify and Cloudflare, `vercel.json` for Vercel.
GitHub Pages ignores custom headers, and the site still works fine there.

## 3. Releasing a new version

1. Build the desktop apps (`./desktop/package.sh` from the repo root). This refreshes `desktop-builds/clean/`.
2. Update `version`, `dateLabel` and `year` in `site.json`.
3. Add a new `<article class="release">` at the top of `src/pages/changelog.html` (and move the `latest` class and badge to it).
4. Run `node build.js`. Downloads, sizes and SHA-256 checksums on every page update automatically.
5. Deploy `dist/` again.

The download filenames are matched by pattern (`windows-x64-Setup`, `_amd64`, `linux-x64-portable`, …), so version
numbers in filenames can change freely.

## Safety: only the CLEAN edition is published

`build.js` copies **only** files with `CLEAN` in the name from `desktop-builds/clean/`. It then refuses to finish
(and deletes `dist/`) if anything named `WITH-KEYS`, `seed-keys` or `gateway.env` ends up in the output.
The personal WITH-KEYS build must never be uploaded to the website.

## Local preview

```bash
node build.js          # full build (copies the ~20 MB of installers)
node build.js --no-dl  # faster rebuild while editing pages (keeps the existing downloads)
node serve.js 3000     # http://localhost:3000
```

## Editing tips

- Page metadata goes on the first line of each page: `<!--meta {"title": "...", "description": "...", "nav": "docs"} -->`
- Placeholders: `{{root}}` (relative path to the site root), `{{version}}`, `{{date}}`, `{{icon:name}}`,
  `{{dl:<pattern>:href|file|size|sha}}`
- Animated orbits: `<canvas data-orbit data-cx data-cy data-size data-mcx data-mcy data-msize>`. They pause when
  off-screen and draw a single still frame when the visitor prefers reduced motion.
- Screenshots are real captures of A.S.T.R.O AI 1.0.0 (Linux build), stored as WebP in `src/static/assets/img/shots/`.
- Fonts (Inter, Instrument Serif, JetBrains Mono) are self-hosted under the SIL Open Font License.
