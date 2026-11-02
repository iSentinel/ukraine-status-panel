# Home page text catalog

Run python3 tests/home-page-text-catalog-native.py --help for options.
Use --build-dir with a new directory to retain all compiler and runtime logs.

Twenty-five exact UA/RU/EN captions are compared for all256 language bytes:
6400 valid comparisons plus512 invalid-key checks (first invalid25 and255).
AddressSanitizer and UndefinedBehaviorSanitizer are enabled. Four compiled
catalog mutations are rejected, including an overly wide key bound.

The consumer check verifies all75 real text bindings, the three language guards
and fifteen footer lookups. Reverse comparison proves the rest of the YAML
is unchanged. The footer test now checks caption ownership instead of assuming
the Home WEATHER caption must remain a literal. Its native golden is unchanged.

Initially hidden widgets are retained. Dynamic Wi-Fi, source state, map counts,
weather and selected-region handling remain outside this text-only change.
No test here contacts or modifies the physical panel.
