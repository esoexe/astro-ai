---
name: apple-design-audit
description: Comprehensive UI/UX reviewer grounded in Apple Human Interface Guidelines (HIG) and WCAG 2.2 Level AA. Audits SwiftUI, UIKit, React Native, Flutter, Tauri, and Web interfaces for typography, touch targets (44x44pt), contrast, Liquid Glass, safe areas, haptics, and platform fidelity.
---

# Apple Design Audit — Human Interface Guidelines & Craftsmanship

Audits applications and websites against Apple's design philosophy: Clarity, Deference, and Depth. Provides precise pixel values, HIG citations, and concrete code fixes across SwiftUI, React Native, Flutter, and Web CSS.

## The Six Audit Lenses

1. **Touch Targets & Geometry**
   - Minimum interactive target size: 44×44 pt (iOS/iPadOS), 28×28 pt (macOS).
   - Corner radius consistency: Apple continuous squircle curves (`smooth-corners`, iOS system continuous curves).
   - Safe Area Insets: Dynamic Island, home indicator padding, fold seam margins.

2. **Typography Scale & Hierarchy**
   - San Francisco (SF Pro / SF Compact) type scale: Large Title (34pt), Title 1 (28pt), Title 2 (22pt), Title 3 (20pt), Headline (17pt semibold), Body (17pt regular), Callout (16pt), Subheadline (15pt), Footnote (13pt), Caption 1 (12pt), Caption 2 (11pt).
   - Dynamic Type support: ensure text scales gracefully without clipping or overlapping.
   - Contrast ratios: WCAG 2.2 AA compliant (minimum 4.5:1 for body text, 3:1 for large text).

3. **Color, Depth & Material (Liquid Glass)**
   - Translucent materials: system ultra-thin, thin, regular, thick materials with background blur (`backdrop-filter: blur(20px)`).
   - Elevation & shadows: soft, diffuse shadows with low opacity (never harsh, opaque black drop-shadows).
   - Dark Mode: true semantic colors (Label, Secondary Label, Tertiary Label, System Background, Secondary System Background).

4. **Motion & Interaction Physics**
   - Spring animations and fluid damping: mass, stiffness, and damping ratios instead of linear or abrupt cubic-bezier transitions.
   - Duration caps: interactive UI feedback should resolve in 150ms-300ms; avoid sluggish animations.
   - Haptic feedback semantics: selection change, success, warning, error.

5. **Navigation & Information Architecture**
   - Navigation bars: Large titles transitioning to inline titles on scroll.
   - Tab bars vs Sidebars: bottom tab bar on iPhone (max 5 items), split view / sidebar on iPad and macOS.
   - Modal presentations: sheet presentation with drag indicator vs full-screen covers.

6. **App Store Review Guidelines Compliance**
   - Guideline 4.0 (Design): Rejecting generic web wrappers, incomplete mockups, non-native button styling.
   - Guideline 4.9: Apple Pay button marks must strictly follow Apple's SVG/CSS vector branding guidelines.
