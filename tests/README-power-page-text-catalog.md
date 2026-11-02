# Power page text catalog

Run python3 tests/power-page-text-catalog-native.py --help for options.
Use --build-dir with a new directory to retain compiler and runtime logs.

Eight exact UA/RU/EN captions are compared for all256 language byte values:
2048 checks with AddressSanitizer and UndefinedBehaviorSanitizer. Unknown
languages use Ukrainian; invalid keys return an empty string.

The consumer test checks all24 real text bindings, three language guards
and fifteen existing footer lookups. Source reverse-equivalence proves that
dynamic source state, schedule, sensor status, styles and actions are unchanged.
Three compiled catalog mutations and three incorrect consumers are retained
in the release preflight proof. The test never contacts the panel.

The permanent fixture additionally checks the first invalid key8 and key255
for every language:512 invalid-key checks. The full suite used the original
fixture; the strengthened fixture passed a separate native run immediately
afterwards, with identical production source fingerprint. The four catalog
mutations (including an overly wide bound) are retained in the boundary proof.
