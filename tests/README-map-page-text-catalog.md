# Map page text catalog

Run python3 tests/map-page-text-catalog-native.py --help for options.
Use --build-dir with a new directory to retain compiler and runtime logs.
The test uses the production header with address/undefined behavior checks.

It compares ten exact labels for all 256 language byte values: 2560 checks.
Unknown languages use Ukrainian; invalid keys return an empty string.
The static check binds all thirty UA/RU/EN updates to their actual widgets
and preserves language guards and footer lookups. Its exact script ID excludes
the separate localize_page_map_full handler.

See the .359 preflight reports for three compiled catalog mutations and three
rejected consumer variants. This test performs no network or panel operation.
