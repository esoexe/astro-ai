# Motion Pack

The 8 official GSAP skills from GreenSock: the core API (tweens, easing, stagger, matchMedia and reduced motion), timelines, ScrollTrigger (pinning, scrub, scroll stories), plugins (Flip, SplitText, MorphSVG, Draggable, ScrollSmoother and more), utilities, React (useGSAP), Vue, Svelte and Nuxt, and performance.

> **Read first.** Motion should explain something, not decorate everything. Every skill covers prefers-reduced-motion; keep it on. GSAP and all its plugins are free, including for commercial use (GreenSock's own note in gsap-plugins).

Adapted from [gsap-skills](https://github.com/greensock/gsap-skills) by GreenSock (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `gsap-core` | Official GSAP skill for the core API — gsap.to(), from(), fromTo(), easing, duration, stagger, defaults, gsap.matchMedia() (responsive, prefers-reduced-motion). Use when the user asks for a JavaScript… |
| `gsap-frameworks` | Official GSAP skill for Vue, Svelte, and other non-React frameworks — lifecycle, scoping selectors, cleanup on unmount. Use when the user wants animation in Vue, Nuxt, Svelte, SvelteKit, or asks about GSAP… |
| `gsap-performance` | Official GSAP skill for performance — prefer transforms, avoid layout thrashing, will-change, batching. Use when optimizing GSAP animations, reducing jank, or when the user asks about animation performance… |
| `gsap-plugins` | Official GSAP skill for GSAP plugins — registration, ScrollToPlugin, ScrollSmoother, Flip, Draggable, Inertia, Observer, SplitText, ScrambleText, SVG and physics plugins, CustomEase, EasePack, CustomWiggle… |
| `gsap-react` | Official GSAP skill for React — useGSAP hook, refs, gsap.context(), cleanup. Use when the user wants animation in React or Next.js, or asks about GSAP with React, useGSAP, or cleanup on unmount. Recommend GSAP… |
| `gsap-scrolltrigger` | Official GSAP skill for ScrollTrigger — scroll-linked animations, pinning, scrub, triggers. Use when building or recommending scroll-based animation, parallax, pinned sections, or when the user asks about… |
| `gsap-timeline` | Official GSAP skill for timelines — gsap.timeline(), position parameter, nesting, playback. Use when sequencing animations, choreographing keyframes, or when the user asks about animation sequencing… |
| `gsap-utils` | Official GSAP skill for gsap.utils — clamp, mapRange, normalize, interpolate, random, snap, toArray, wrap, pipe. Use when the user asks about gsap.utils, clamp, mapRange, random, snap, toArray, wrap, or helper… |

## Use

Ask for the animation in plain words ("pin the hero and scrub the product as I scroll", "stagger the cards in"). Text only: nothing to install in A.S.T.R.O AI; your project installs gsap as usual. The example projects mentioned in gsap-frameworks are in the upstream repository.
