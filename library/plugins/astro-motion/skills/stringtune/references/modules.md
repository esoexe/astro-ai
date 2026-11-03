# StringTune module reference (1.2.x)

Short summary in our own words. Each module has a full page at
`https://tune.fiddle.digital/docs/modules/<slug>`. Register a module with `st.use(Module)` before
`st.start()`. "Element" modules connect to elements with `string="<key>"`. "Global" modules work
once registered.

## Shared attributes (all element modules)

| Attribute | Default | Meaning |
|---|---|---|
| `string` | required | module keys, `\|`-separated; optional mode list in brackets: `lerp[smooth]` |
| `string-id` | `string-N` | stable id used by object events |
| `string-copy-from` | none | mirror the output of one or more source ids (`a\|b`) |
| `string-key` | `--progress` | rename the variable a module writes |
| `string-offset-top` / `string-offset-bottom` | `0%` | shift timeline end / start (`px`, `%`, `sh`) |
| `string-inview-top` / `string-inview-bottom` | `0%` | shift the in-view window (`-inview` class, `object:inview:<id>`) |
| `string-enter-el` / `string-enter-vp` | `top` / `bottom` | edges that start the timeline |
| `string-exit-el` / `string-exit-vp` | `bottom` / `top` | edges that end the timeline |
| `string-repeat` | false | remove `-inview` again when the element leaves |
| `string-self-disable` | false | no style writes on the source (mirrors still get them) |
| `string-outside-container` | false | observe against the window, not a custom scroll container |

Every attribute also works as `data-string-*`.

## Scroll

| Module | Key | Writes | Attributes (default) | Event |
|---|---|---|---|---|
| StringProgress | `progress` | `--progress` (eased, 0..1) | `string-easing` (`cubic-bezier(0.25, 0.25, 0.25, 0.25)`), geometry above | `object:progress:<id>` |
| StringProgressPart | `progress-part` | `--progress-slice` on itself | `string-part-of="source-id[0.25-1]"` | `object:progress-slice:<id>` |
| StringParallax | `parallax` | inline `translate3d` | `string-parallax` (0.2, sign = direction), `string-parallax-bias` (0) | `object:parallax:<id>` |
| StringLerp | `lerp` | `--lerp` (signed scroll velocity, 0 at rest) | none; smooth mode only by default | `object:lerp:<id>` |
| StringGlide | `glide` | `--glide` plus inline `translate3d` | `string-glide` (1); smooth mode only | `object:glide:<id>` |
| StringScrollContainer | `scroll-container` | none | `string-lerp` (0.1) | none |
| StringScroller | `scroller` | none (forwards wheel to main scroll) | none | `wheel` |
| StringScrollbar | global | custom scrollbar | not documented yet; read the package types | none |

## Text

| Module | Key | Writes | Attributes | Notes |
|---|---|---|---|---|
| StringSplit | `split` | `.-s-line`, `.-s-word`, `.-s-char` spans; `--line-index`, `--word-index`, `--char-index`, `--kerning`, totals; `--fit-font-size` | `string-split` tokens `line`, `word`, `word-line`, `char`, `char-line`, `char-word`, `fit`, each with `[start\|center\|end\|random(a,b)]`; `string-split-restore-after` (ms) | keeps inline HTML; `<split-class class="x">` adds classes to generated wrappers |
| StringCircularText | not documented yet; read the package types | text on a circle | undocumented | |

## Pointer

| Module | Key | Writes | Attributes (default) | Event |
|---|---|---|---|---|
| StringCursor | global; targets `string="cursor"` | target `--x`, `--y` (plus `--x-px`, `--y-px`, `--dx`, `--dy`, `--angle`, `--angle-deg` via `string-cursor-vars`); portal `--x`, `--y` (px), `--x-lerp`, `--y-lerp`, class `-show` | target: `string-cursor-target` (`default`, `*` = all portals), `string-cursor-class`, `string-alignment` (`center`), `string-cursor-enter` (`snap`), `string-cursor-leave` (`smooth`), `string-cursor-leave-hold`, `string-lerp`; portal: `string-cursor` (`default`), `string-cursor-lerp` (0.75) | `cursor:start\|move\|pixel\|end:<id>`, `cursor` |
| StringMagnetic | `magnetic` | `--magnetic-x`, `--magnetic-y` | `string-strength` (0.3), `string-radius` (150) | `magnetic:move:<id>` |
| StringImpulse | `impulse` | `--push-x`, `--push-y`, `--push-rotation` | `string-position-strength` (3), `-tension` (0.05), `-friction` (0.15), `string-max-offset` (220), `string-rotation-strength` (0.75), `string-rotation-max-angle` (18), `string-rotation-origin` (`center center`), `string-continuous-push` (true) | `object:impulse:<id>:move\|rotate\|side` |
| StringSpotlight | `spotlight` | `--spotlight-angle` (deg), `--spotlight-distance` | `string-lerp`, `string-angle-threshold` (0.2), `string-distance-threshold` (0.5), `string-deadzone` (4), `string-dist-max` (0) | `spotlight:update:<id>` |
| StringTilt, StringAttractor, StringVelocity | not documented yet; read the package types | | | |
| StringAnchor | `anchor` | inline `transform-origin` | `string-anchor` (`center center`, accepts `random(a,b)`) | none |

Magnetic, Impulse and Spotlight do not need StringCursor registered.

## Media, layout and forms

| Module | Key | Behaviour | Attributes | Event |
|---|---|---|---|---|
| StringLazy | `lazy` on `<img>` | loads the real source when in view; classes `lazyLoad`, `-aspect-ready`, `-loaded` | `string-lazy="<url>"` | `image:load:all` |
| StringVideoAutoplay | `autoplay` on `<video>` | muted inline loop; plays in view, pauses out of view | `string-src="<url>"` | none |
| StringMasonry | `masonry` | measured masonry layout | `string-masonry-cols`, `string-masonry-gap` (per breakpoint), `string-masonry-mode` (`auto`) | `masonry:shuffle:start\|end`; send `masonry:update:<id>` |
| StringMarquee | not documented yet; read the package types | scrolling marquee | undocumented | |
| StringSequence | not documented yet; read the package types | image or frame sequence | undocumented | |
| StringForm | `form` on `<form>` | declarative validation; fields use `string-input="rule\|list"`, helpers `string-input="error[key]"` | not documented yet; read the package types | `form:submit:<id>`, `form:invalid:<id>`, `form:field:valid\|invalid\|error:<key>` |
| StringResponsive | global | shows/hides `string-mobile` (0-359), `string-tablet` (360-1023), `string-laptop` (1024-1365), `string-desktop` (1366+); ranges configurable via `use(StringResponsive, { settings })` | element flags | `screen:mobile\|tablet\|laptop\|desktop` (stateful) |
| StringLoading | global | ready state after `timeout` (900 ms) | `use(StringLoading, { timeout })` | |

## Debug

StringFPSTracker (`fps` event), StringPositionTracker (`scroll-position`), StringLerpTracker,
StringDelayLerpTracker, and the dev tools (StringDevLayout, StringDevRulers, StringDevInview,
StringDevProgress). Do not ship them in production builds.

## Global settings

`st.setupSettings({ 'offset-top': '-10%', 'offset-bottom': '-10%', parallax: 0.3, 'cursor-lerp': 0.75, timeout: 900 })`
sets fallbacks for every element. Smooth-scroll feel is not a setting: use `st.speed` and
`st.speedAccelerate`.

## Skill Hub examples (string-tune.fiddle.digital/skill-hub)

Basics: reveal on scroll, parallax, progress, lerp, glide, cursor, magnetic, spotlight, impulse,
split, three form examples, FPS tracker, position tracker, sequence. Typography: text with
progress (two), kinetic text, split and lerp. Layouts: footer shifting, orchestrated masonry.
Use them for visual reference; write your own code.
