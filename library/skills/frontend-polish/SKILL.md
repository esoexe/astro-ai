---
name: frontend-polish
description: Polish a web UI for accessibility, responsive layout, interaction states and visual consistency. Use when building or reviewing pages, components or forms.
---

# Frontend polish

## Checklist

**Accessibility (WCAG 2.2 AA)**
- Semantic elements first (`button`, `a`, `nav`, `main`, `label`), ARIA only to fill gaps.
- Every input has a visible label; errors are announced and linked with `aria-describedby`.
- Full keyboard use: logical tab order, visible focus ring, `Esc` closes dialogs, focus is trapped in and returned from modals.
- Text contrast at least 4.5:1 (3:1 for large text and UI parts); never colour alone to convey meaning.
- Images have useful `alt` text (empty for decoration); motion respects `prefers-reduced-motion`.

**Layout**
- Works from 320 px wide up; no horizontal scrolling; touch targets around 44 px.
- Fluid type and spacing from one scale; long words, long names and translations do not break the layout.

**States**
- Every data view has loading, empty, error (with a retry) and success states.
- Buttons show progress and are protected against double submit; destructive actions ask for confirmation or offer undo.

**Consistency and speed**
- Reuse the project's design tokens and components instead of new one-off styles.
- Images sized and lazy-loaded, fonts with `font-display: swap`, no layout shift when content arrives.

## Output

List issues by severity with the element, the problem and the fix (code). Then apply the fixes if asked.
