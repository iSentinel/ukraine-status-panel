# Room history timestamp ordering

Run python3 tests/room-history-time-order-native.py --help.
The fixture compiles the actual production component with ASAN/UBSAN, checks 3000 relative-time combinations and ring wrap, and requires exact executable failures from missing-guard and reject-equal negative controls. Compilation failure is never a detected mutation.
Optional --output saves evidence in a new directory. CI uses temporary copies for negative controls and checks canonical source hashes before/after.
These are software checks; no physical sensor readings or real clock adjustment are injected. The storage remains RAM-only, 288 samples/3456 bytes.
