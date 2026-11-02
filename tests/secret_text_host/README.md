# Write-only text native tests

The runner compiles actual project secret_text.cpp/header with controlled
preferences and Text state callbacks. It copies the real installed legacy
TemplateTextSaver<64> and setup body (class renamed only) for compatibility.
Only unused parameter warnings in unchanged vendor declarations are suppressed.
All new code retains Wall/Wextra/Werror and ASAN/UBSAN.

Synthetic cases: old computed key/65-byte format restoration, empty public
state on restore/input, internal setter delivery, zero-filled writes, clear,
transient WiFi restart, max64/reject65, failed-save retry and corrupt length.
This does not connect to a device or inspect real credentials.
