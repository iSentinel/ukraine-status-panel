# Web UI — Page Override

**Mode:** Operate / local device configuration  
**Runtime:** embedded, Offline/local-first

## Primary question

The active tab must make it obvious what state is current and what the user can safely do next.
Common actions stay visible; advanced/static-IP/recovery actions use progressive disclosure.

## Shell and navigation

- Keep the existing compact top tabs; do not add a sidebar just for style.
- Navigation recedes: neutral chrome, one active cue, no large saturated active pill.
- Product/device identity and connection state remain visible without becoming a hero banner.
- At 320px tabs scroll horizontally; content never forces horizontal page scroll.

## Surfaces

- Opaque dark surfaces only. No glass, blur, glow, scanlines, gradients or hover-lift cards.
- Cards are for coherent controls/data groups, not for every sentence.
- Structural panels use border/fill steps; shadows only for floating suggestions/popovers.
- Keep the radius vocabulary small and nested radii smaller than parent radii.

## Type and data

- System UI sans only; no remote fonts.
- Tabular numerals for changing values.
- Status and missing/error states use text plus semantic color.
- Never invent values to make a card look populated.

## Controls

- Minimum 44px touch target for primary interactive controls.
- Visible `:focus-visible` state.
- Disabled remains readable; reason is shown in copy when the UI knows it.
- Press feedback may use subtle `scale(.98)` at ~120–160ms; it must not delay the action.
- Destructive actions are visually distinct from everyday settings.

## Motion

- No scroll reveal, parallax, glitch, animated gradients or decorative looping motion.
- Frequent tab/navigation changes are immediate.
- State feedback only; exact properties, never `transition: all`.
- Respect `prefers-reduced-motion`.

## Hardening

Test 320/375/768/1024/1440, long UA/RU/EN labels, large values, empty/one/many rows,
offline/stale/error states and 200% zoom where practical. Keep the embedded Web size gate.
