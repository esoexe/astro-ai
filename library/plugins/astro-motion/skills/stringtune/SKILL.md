---
name: stringtune
description: "StringTune (Fiddle.Digital, MIT): CSS-first, attribute-driven smooth scrolling and web animation. Scroll progress as a CSS variable, parallax, split text, magnetic buttons, cursor followers, spotlight, impulse tilt, lazy images, video autoplay, masonry, form validation and responsive show and hide, all from HTML attributes with a light JS bootstrap. Use when the user mentions StringTune or string-tune, wants smooth scroll with CSS-driven effects, attribute-based animation without writing timelines, or wants to drive GSAP timelines from StringTune scroll progress."
---

# StringTune

StringTune (`@fiddle-digital/string-tune`, MIT, by Fiddle.Digital) is a small modular
library for smooth scrolling and core web animation. You register the modules you need,
mark elements with `string="..."` attributes, and the library writes **CSS variables**,
inline transforms, classes and events. Your CSS does the actual animation.

It is not GSAP and not a GSAP plugin. Use it alone for CSS-driven effects, or with GSAP
(see "With GSAP" below) when you want StringTune's scroll engine and GSAP's timelines.

Official docs: https://tune.fiddle.digital/docs/overview. Live examples: the Skill Hub at
https://string-tune.fiddle.digital/skill-hub. Module details: `references/modules.md`.
These notes match StringTune 1.2.x; check the docs if the project uses another version.

## Setup (once, client side)

```bash
npm install @fiddle-digital/string-tune
```

```ts
import StringTune, { StringProgress, StringParallax, StringSplit } from '@fiddle-digital/string-tune';

const st = StringTune.getInstance();      // one shared instance
st.use(StringProgress);                   // register only what the page uses
st.use(StringParallax);
st.use(StringSplit);
st.start(60);                             // prepares the DOM and starts the frame loop
```

Rules:
- Register modules **before** `start()`, as part of app bootstrap.
- Run it only in the browser: `useEffect` in React/Next.js (in a `'use client'` component),
  a `*.client.ts` plugin started on `app:mounted` in Nuxt, `onMounted` in Vue.
- From a CDN build the classes live under the namespace: `StringTune.StringTune.getInstance()`,
  `StringTune.StringProgress`.
- After injecting or rearranging a lot of DOM, call `st.onResize(true)` to rebuild layout.
  After moving one element, `st.invalidateCenter('<string-id>')`.

## Markup

```html
<section string="progress" string-id="hero">...</section>
<div string="progress|parallax" string-parallax="0.3"></div>   <!-- several modules: | -->
```

- React and Next.js strip unknown attributes: use `data-string`, `data-string-id`,
  `data-string-parallax` and so on. The runtime reads `string-*` first, then `data-string-*`.
- `string-id` gives a stable id; object events use it (`object:progress:hero`).
  Without it the runtime generates `string-N`.
- `string-copy-from="hero"` makes a separate element mirror the source's output
  (for modules that mirror: Progress, Lerp, Glide, Magnetic, Impulse, Spotlight, cursor targets).

## Consume the output in CSS

Variables are plain numbers: add units yourself.

```css
.bar   { transform: scaleX(var(--progress, 0)); transform-origin: left; }
.btn   { transform: translate3d(calc(var(--magnetic-x, 0) * 1px), calc(var(--magnetic-y, 0) * 1px), 0); }
.card  { transform: translate(calc(var(--push-x, 0) * 1px), calc(var(--push-y, 0) * 1px)) rotate(calc(var(--push-rotation, 0) * 1deg)); }
```

Parallax and Glide write `transform` themselves; do not set your own transform on the same
element (wrap it instead).

## Common recipes

| Effect | Markup | Your CSS uses |
|---|---|---|
| Scroll progress 0..1 | `string="progress" string-id="s1"` | `--progress` (rename with `string-key="--p"`) |
| Sub-range of one progress | `string="progress-part" string-part-of="s1[0-0.5]"` | `--progress-slice` |
| Parallax layer | `string="parallax" string-parallax="0.2"` | nothing (inline transform) |
| Split text reveal | `string="split" string-split="line[center]\|char-line[center]"` | `.-s-line`, `.-s-char`, `--line-index`, `--char-index` |
| Magnetic button | `string="magnetic" string-strength="0.3" string-radius="150"` | `--magnetic-x`, `--magnetic-y` |
| Cursor-local values | `string="cursor"` (register `StringCursor`) | `--x`, `--y` (-1..1 with center alignment) |
| Custom cursor follower | an element with `string-cursor="default"` | `--x`, `--y` in viewport px, class `-show` |
| Spring tilt on hover | `string="impulse"` | `--push-x`, `--push-y`, `--push-rotation` |
| Light that follows the pointer | `string="spotlight"` | `--spotlight-angle`, `--spotlight-distance` |
| Lazy image | `<img string="lazy" string-lazy="/img.jpg">` | classes `-loaded`, `-aspect-ready` |
| Autoplay video in view | `<video string="autoplay" string-src="/clip.mp4">` | nothing (muted inline play/pause) |
| Device show/hide | `string-mobile`, `string-desktop` (register `StringResponsive`) | nothing (inline display) |

Timing of progress: the timeline starts when the element edge `string-enter-el` (default `top`)
meets the viewport edge `string-enter-vp` (default `bottom`), and ends when `string-exit-el`
(`bottom`) meets `string-exit-vp` (`top`). Shift it with `string-offset-top` and
`string-offset-bottom` in `px`, `%` (of viewport height) or `sh` (of the element's own height);
negative values fire earlier.

## Smooth scroll

- Defaults: smooth scrolling on desktop, native on mobile.
  `st.scrollDesktopMode = 'smooth' | 'default' | 'disable'`, same for `scrollMobileMode`.
- Feel: `st.speed` (0.07 soft, 0.1 balanced, 0.16 direct) and `st.speedAccelerate` (0..1).
- Scroll in code: `st.scrollTo('#pricing')`, `st.scrollTo({ selector: '#pricing', offset: -80 })`,
  `st.scrollTo({ position: 0, immediate: true })`.
- Lerp, Glide and Parallax run in smooth mode only by default. Force a mode list per element:
  `string="parallax[]"` (all modes), `string="lerp[smooth|default]"`.

## Events

```ts
const onP = (v: number) => { /* 0..1 */ };
st.on('object:progress:hero', onP);
st.off('object:progress:hero', onP);    // always remove in unmount/cleanup
```

Others: `object:parallax:<id>`, `object:inview:<id>`, `magnetic:move:<id>`, `cursor:move:<id>`,
`screen:mobile` (stateful), `form:submit:<id>`, `image:load:all`, `fps`.

## With GSAP

Let StringTune own the scroll and GSAP own the choreography: build a paused timeline and set
its progress from the StringTune event. Do not also attach a ScrollTrigger to the same element.

```ts
import gsap from 'gsap';
const tl = gsap.timeline({ paused: true })
  .from('.hero h1', { yPercent: 100, opacity: 0 })
  .to('.hero img', { scale: 1.15 }, 0);
st.on('object:progress:hero', (p: number) => tl.progress(p));
```

## Accessibility and performance

- Respect `prefers-reduced-motion`: in CSS, neutralise transforms driven by the variables, and
  in JS consider `st.scrollDesktopMode = 'default'` for those users.
- Keep text in the DOM (StringSplit keeps inline HTML); avoid splitting very long paragraphs
  into characters.
- Custom modules must batch DOM reads and writes with `frameDOM.measure()` and
  `frameDOM.mutate()` from the package; never read layout and write styles back to back.
- Use only the documented public API (exports, attributes, events). Not public:
  `scrollToElement()`, `invalidateCenters()`.

StringTune-3D (`string-tune-3d`, a separate community project, MIT, early 0.0.x) drives three.js
objects from CSS variables; treat its API as unstable and read its own README first.
