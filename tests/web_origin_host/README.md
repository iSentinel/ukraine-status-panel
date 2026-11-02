# Custom route Origin middleware tests

Actual WebServerBase middleware and owned credential store are compiled with
controlled HTTP metadata using the same boundary as ownership/concurrency QA.
All request/body/upload callbacks are exercised. Tests cover auth-before-origin,
cross-site denial, local/absent Origin compatibility, custom-only scope, methods,
hostname case, IPv4/IPv6 literal spelling, default/explicit ports and malformed
authorities. Standard routes remain subject to WebServer's existing policy.

No device, credentials, TLS connection, browser login or mutation endpoint is
used. ASAN/UBSAN are enabled. Actual ESP32 syntax/build is a separate gate.
