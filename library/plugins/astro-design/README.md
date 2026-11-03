# Design Pack

Professional design for websites, 3D sites, web apps, mobile apps and desktop software.

> **Read first.** These skills raise the floor: a design system before code, one clear direction, real components, and a visual check at phone, tablet and desktop sizes. They do not guarantee a result a human designer would sign off. Look at what the agent built and ask for changes. The 21st.dev, shadcn, Magic UI, Playwright and Chrome DevTools servers are separate Library items; 21st.dev needs your own key and its free plan has a small monthly allowance.

Adapted from [UI/UX Pro Max](https://github.com/nextlevelbuilder/ui-ux-pro-max-skill) by Next Level Builder (MIT) and [frontend-design](https://github.com/anthropics/skills/tree/main/skills/frontend-design) by Anthropic (Apache-2.0). The design director is written by the Astro AI team. See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `design-director` | Runs the whole job: a written brief, the design system first, one visual direction, the right stack, real components from the 21st.dev, shadcn and Magic UI servers when installed, 3D and motion within a performance budget, then a visual check before hand-off. |
| `ui-ux-pro-max` | Design intelligence with a built-in search: 79 styles, 192 product palettes and reasoning rules, 74 font pairings, 119 UX guidelines, 105 icons, 17 GSAP motion presets, 25 chart types and rules for 22 stacks. Generates a full design system and saves it as `design-system/<project>/MASTER.md`. |
| `frontend-design` | Anthropic's guidance for distinctive, intentional visual design that does not read as a template. |

## Use

Ask in plain words ("build a landing page for my bakery, make it feel premium") or name a skill.
The search runs inside A.S.T.R.O AI (1.9.5 or later): nothing to install, no Python, no network.

```bash
${ASTRO_TOOL} ui-ux-pro-max/search "bakery artisan premium" --design-system -p "Crumb & Co"
${ASTRO_TOOL} ui-ux-pro-max/search "form validation" --domain ux
${ASTRO_TOOL} ui-ux-pro-max/search "virtualized list" --stack react-native
```

Works well with the **3D Pack** (three.js and React Three Fiber) and the **Motion Pack** (GSAP).
