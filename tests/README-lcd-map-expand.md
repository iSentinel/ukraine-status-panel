# Full-map button check

Run python3 tests/lcd-map-expand-static.py --help for usage.

The test retains the80x48 minimum touch area, vertical placement and8-pixel
clearances. The three UA/RU/EN updates must bind the real button to the
Home catalog's MAP_EXPAND key. Exact caption bytes are independently checked
by tests/home-page-text-catalog-native.py; this layout test does not require
them to remain inline YAML literals. It performs no device action.
