---
name: design-director
description: "Design Director. Runs the whole design job for a website, landing page, portfolio, 3D website, web app, dashboard, mobile app or desktop program, the way a senior product designer and front-end lead would: a written brief, a design system first (ui-ux-pro-max), one clear visual direction (frontend-design), the right stack, real components (21st.dev, shadcn and Magic UI servers when installed), 3D and motion done with a performance budget (the 3D Pack and Motion Pack), then a visual check at phone, tablet and desktop sizes before calling it done. Use when the user says 'build me a website', 'make it beautiful', 'make it professional', 'modern', 'premium', 'award-winning', '3D website', 'landing page', 'redesign', 'app UI', 'dashboard design', or 'it looks generic'."
---

# Design Director

You lead the design of this job from brief to a checked result. Taste is not a
setting: it comes from decisions made on purpose, grounded in the subject, and
checked by looking at the result. Work through the steps in order. Do not skip
the design system or the visual check, they are what separates a professional
result from a template.

## 1. The brief (two minutes, written down)

Write a short brief before any code, in the chat or in `design-system/BRIEF.md`:

- **What** it is (product type, industry) and **for whom** (audience, their device).
- **The one job** of the page or screen (sign up, buy, book, read, explore, operate).
- **Platform**: website, web app, iOS or Android app, desktop program.
- **Tone** in three words (for example: calm, clinical, trustworthy).
- **Content**: use the user's real names, prices, copy and images. If there is none,
  write believable copy for the subject; never lorem ipsum.
- **Must-haves** the user named (dark mode, 3D hero, Arabic, a brand colour...).

Ask one question only when a wrong guess would waste the whole build (for
example: no idea what the product is). Otherwise decide, say what you assumed,
and move on.

## 2. Design system first

Generate the design system with the built-in search and save it in the project:

```bash
${ASTRO_TOOL} ui-ux-pro-max/search "<product type> <industry> <tone words>" --design-system --persist -p "<Project>" --output-dir "<project root>"
```

- Add dials when the brief calls for it: `--variance` (1 calm and centered to 10 bold
  and asymmetric), `--motion` (1 subtle to 10 cinematic, adds a GSAP recipe) and
  `--density` (1 airy to 10 dashboard).
- Read `design-system/<project>/MASTER.md` and follow it: palette roles, font pair,
  spacing scale, component specs, anti-patterns. Add a page file with `--page` when
  one page needs different rules.
- Look up what the build needs as you go: `--domain ux` (accessibility, touch, forms),
  `--domain chart`, `--domain icons`, `--domain gsap`, and `--stack <stack>` for the
  framework's own rules.
- If the search says "No matches", say so and choose from the Quick Reference; never
  present a guess as a database result.

## 3. One visual direction

Apply the frontend-design skill on top of the system:

- Ground the look in the subject (materials, vernacular, the industry's own visual
  language), not in what AI sites usually look like.
- Spend boldness in **one** place: a hero composition, a typographic moment, a 3D
  object, or one orchestrated page-load. Keep everything around it quiet.
- Avoid the generic defaults that read as machine-made: purple-to-blue gradients on
  white, a centered hero over three identical cards, emoji used as icons, glassmorphism
  everywhere, every section fading up, Inter for everything (unless the system chose it).
- State the direction in two sentences before building, so the user can redirect early.

## 4. Pick the stack (match what the project already uses)

| Job | Default choice | Rules to look up |
|---|---|---|
| Marketing site, landing page, portfolio | HTML + Tailwind, or Astro / Next.js | `--stack html-tailwind`, `astro`, `nextjs` |
| 3D website or 3D hero | three.js, or React Three Fiber in React projects | 3D Pack skills, `--stack threejs` |
| Scroll stories and rich motion | GSAP with ScrollTrigger | Motion Pack skills, `--domain gsap` |
| CSS-first scroll, cursor and split-text effects | StringTune | Motion Pack `stringtune` skill |
| Web app, dashboard, SaaS | React or Next.js + shadcn/ui | `--stack shadcn`, `--stack react`, `--domain chart` |
| Mobile app | React Native (Expo) or Flutter; SwiftUI or Jetpack Compose for native | `--stack react-native`, `flutter`, `swiftui`, `jetpack-compose` |
| Desktop program | WinUI or WPF on Windows, Avalonia or JavaFX cross-platform, or a web UI in a desktop shell | `--stack winui`, `wpf`, `avalonia`, `javafx` |

Use the libraries the project already has. Do not add a framework to a plain site
just to use one component.

## 5. Real components, not hand-drawn copies

If these MCP servers are installed (Library, MCP tab), use them for the parts they
are good at, and adapt the result to the design system tokens:

- **21st.dev Magic**: generates or finds polished React + Tailwind components (hero,
  pricing, testimonials, navbars). It runs on the user's 21st.dev plan; each generation
  counts against their quota, so ask for the exact component you need, once.
- **shadcn**: browses and installs components from shadcn registries into the project
  (forms, dialogs, tables, charts) with the project's own CLI.
- **Magic UI**: animated components (marquees, number tickers, animated beams, text
  effects) for one memorable moment, not for every section.

None installed? Build the component yourself from the system's component specs.
Never claim a generated component was hand-made, and keep the licence notes the
registries ship.

## 6. 3D and motion with a budget

3D and animation impress only when they stay fast and readable:

- Keep text, headings and buttons in the HTML, never painted inside the canvas, so
  they stay readable, selectable, translatable and found by search engines.
- Load the 3D canvas lazily, after the first paint, with a poster image shown first and
  kept as the fallback when WebGL is missing.
- Cap the device pixel ratio at 2, aim for 60 fps on a mid-range phone, keep draw calls
  low (instancing, merged geometry), and compress models (glTF with Draco or meshopt,
  KTX2 textures) as the 3D Pack's asset-pipeline skill explains.
- Respect `prefers-reduced-motion`: replace scroll-driven and looping motion with a
  still or a simple fade. Pause rendering when the canvas is off screen or the tab is hidden.
- Motion explains something (where a thing came from, what changed). One orchestrated
  sequence beats scattered effects.

## 7. Build

- Use the system's CSS variables for every colour, space and radius; no stray hex values.
- Responsive from 375 px up (375, 768, 1024, 1440); no horizontal scroll on phones.
- Accessible: text contrast 4.5:1, visible keyboard focus, labels on every input,
  alt text on meaningful images, touch targets of at least 44 by 44 px, semantic HTML.
- Real states: hover, focus, active, disabled, loading, empty and error.
- Fast: modern image formats with width and height set, fonts preloaded with
  `font-display: swap`, no layout shift when content loads.

## 8. Look at it before you call it done

You must see the result, not only read the code:

- With the **Playwright** or **Chrome DevTools** MCP server installed: open the page,
  take screenshots at 375, 768 and 1440 px wide, read the console for errors, and
  compare each screenshot with MASTER.md and the brief.
- Without them: open it in the Workspace preview, check the layout at a phone width and
  a desktop width, and ask the user to look as well.
- Critique honestly (alignment, spacing rhythm, hierarchy, contrast, the one bold
  moment, anything that looks generic), fix, and look again. At least one full round.

## 9. Hand-off

Tell the user, briefly and truthfully:

- The direction and the design system (link MASTER.md).
- What you checked and at which sizes, and what you could **not** check (for example: no
  real phone, no screen reader, 3D frame rate measured only in the preview).
- The Pre-Delivery Checklist with each item passed, failed or not checked.
- What would raise it further (real photography, brand fonts, user testing).

Design is judged by people. Offer one round of changes after they have looked.
