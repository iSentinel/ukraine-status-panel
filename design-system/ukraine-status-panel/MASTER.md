# Ukraine Status Panel — Authoritative Design System

**Mode:** Operate / monitoring dashboard  
**Target:** Waveshare ESP32-S3-Touch-LCD-7B, 1024×600 + embedded local Web UI  
**Design dials:** variance 3/10, motion 1/10, density 8/10

This file is authoritative. Page-specific files in `design-system/ukraine-status-panel/pages/`
override it only where they explicitly say so. Historical generated recommendations for
Glassmorphism, neon/glitch decoration, hover-lift cards, remote fonts and decorative motion are
retired for this project.

## Product idea

This is an operational status surface, not a marketing page. The visual identity comes from the
product itself: the Ukraine map, live status data, power state, weather, room telemetry and network
integrations. Chrome recedes; data carries the visual emphasis.

Each screen must answer one primary question first:

- Home — what needs attention now?
- Map — where is the active threat/status and how fresh is it?
- Weather — what is the current/next weather for the selected place?
- Power — is power healthy now and what changed recently?
- History — what actually happened over time?
- Clock — what time/date is trusted now?
- Settings — what can I change safely, and what will that setting affect?
- Diagnostics — what is wrong and what evidence can I collect?

## Color

### Core surfaces

| Role | LCD | Web |
| --- | --- | --- |
| Deep background | `#07111F` | `--color-background-deep` |
| Page background | `#0F172A` | `--color-background` |
| Primary card/panel | `#0C1828` | `--color-card` |
| Working/secondary surface | `#101E30` only when a second layer is needed | `--color-muted` |
| Structural border | `#263D57` | `--color-border` |
| Primary text | `#F8FAFC` / white | `--color-foreground` |
| Secondary text | `#91A8B2` minimum | `--color-muted-foreground` |

The accepted dark-blue identity is preserved. Do not replace it with generic black solely because a
reference prefers black. Refinement preserves identity.

### Semantic color only

Color is for data, state, selection and primary action — not decoration.

- clear/success: `#1F8A4C`
- UAV / warning: `#F5BE26`
- generic air/KAB/MiG warning: `#F97316`
- missile/error: `#DC3545`
- ballistic: `#A855F7`
- info/current selection: blue family already used by the project
- no data/neutral: `#34424E`

A color state always has a text/shape cue. Firmware version, manual policy, labels and decorative
metadata are neutral, not green.

## Typography

- One UI sans family per surface. No monospace as a “technical” costume.
- Use tabular numerals for values that update or align in columns.
- LCD hierarchy: metadata 18px, body/control 18–22px, section title 22–24px, page title 28–32px.
  Do not use landing-page scale for app screens.
- Web hierarchy stays compact: body 13–16px, labels/meta 12–14px, page title roughly 24–32px.
- Sentence case for working UI. Uppercase is reserved for short section/system labels where the
  current product already uses it consistently.
- Long UA/RU/EN strings, negative values and large counts are first-class test cases.

## Layout and density

- Prefer groups separated by spacing or one 1px rule over nested cards.
- A border or a shadow, never both. Shadows are reserved for floating UI such as suggestions,
  popovers and dialogs.
- Keep a small radius vocabulary: controls 8–10px, panels 12–16px. A child radius is never larger
  than its parent.
- The block that answers the screen’s primary question is visually strongest; secondary metrics must
  not outshout it.
- Repeated numbers are sourced from one value. Missing data is a dash/status, never a fabricated zero.
- Navigation recedes. Active state may use one neutral fill plus one clear selection cue; avoid large
  saturated pills as the default navigation language.

## Components

### Buttons and controls

- LCD touch targets ≥48px for primary navigation/controls where geometry permits.
- Web touch targets ≥44 CSS px.
- One visual vocabulary for primary/secondary/destructive actions.
- Destructive actions name the consequence and use destructive color only for the destructive action.
- Disabled controls remain legible; do not hide them behind low opacity.
- Focus is visible on Web.
- Press feedback may be near-imperceptible (`scale(.98)` / ~120–160ms) when it is cheap. It must not
  delay the action.

### Data/status

- Status color is never the only carrier of meaning.
- Charts use truthful scales, explicit units and gaps for missing samples.
- One subject keeps one hue across a screen; unrelated rainbow series are forbidden.
- Forecasts, advisory data, approximate threat positions and measured observations are visually and
  textually distinct.

### Settings

- Organize by area and job, not by a giant undifferentiated list.
- Label + explanation sit next to the control where space allows.
- One save model per view.
- Show the result/meaning of a setting near the control when practical.
- Diagnostics and destructive/system actions are visually separated from everyday preferences.

## Motion

Frequent navigation and functional data do not get decorative animation.

Motion is allowed only for:

1. immediate feedback,
2. state indication,
3. spatial consistency,
4. bridging a jarring appearance/disappearance.

Rules:

- frequent navigation: instant or near-instant;
- button feedback: ~120–160ms;
- occasional small state transitions: ~150–250ms;
- no page-load choreography;
- no animation whose correctness depends on an animation-end callback;
- no full-frame slide animation on the LCD unless measured frame/bandwidth budget passes first;
- respect reduced motion on Web.

The `.235` hardware experiment is authoritative for the current LCD pipeline: full-frame CPU/GDMA
copy cannot meet the accepted frame-gap budget, so page-transition animation remains disabled.

## Accessibility and hardening

- Text contrast ≥4.5:1 for normal text; ≥3:1 only for legitimately large text.
- Do not use emoji as structural icons.
- Test UA/RU/EN, long labels, missing fields, huge counts, negative temperatures, zero/one/many,
  stale/error/offline states and absent optional hardware.
- Web: keyboard focus, 320/375/768/1024/1440 widths, 200% zoom where practical.
- LCD: physical finger acceptance is separate from synthetic pointer QA.

## Performance floor

- Preserve fb0/fb1 LVGL DIRECT + fb2 resident Home cache.
- No additional full-screen buffer without measured memory + bandwidth proof.
- Update only the region/widget that changed.
- Hidden/offscreen work is deferred.
- Web runtime size gate remains mandatory; design changes do not bypass it.
- No remote runtime fonts, icon CDNs, animation libraries or decorative network assets.

## Sources adapted for this project

- `pbakaus/impeccable` — Operate mode, craft floor, preserve incumbent identity, verify states/contrast.
- `Leonxlnx/taste-skill` — app-screen hierarchy, color-on-data, flat surfaces, honest data, settings rules.
- `emilkowalski/skills` — motion frequency/purpose gate, press feedback, frame-budget discipline,
  adversarial worst-case UI testing.

The project constraints and measured hardware behavior win whenever a generic web recommendation
conflicts with the ESP32-S3/LVGL target.

## Design Polish v2 — shared LCD vocabulary

Candidate `0.115.402-dev` starts with a bounded cross-screen polish slice.
The first slice keeps geometry, data ownership and navigation unchanged while
normalizing the visible chrome used by Home, Map, Weather, Power/Shelly, Room
and Spotify.

- primary panels remain radius 16, `#0C1828`, 1 px `#263D57`;
- page titles use 28 px and `#F8FAFC`;
- Home card titles use 20 px and `#F8FAFC`;
- secondary LCD controls use radius 10;
- compact Home status badges use radius 8;
- semantic fills remain owned by the data/state, never by decoration.

A dedicated static gate checks all three firmware profiles so future screens
cannot silently drift from this vocabulary.
