# Starlink GPS component

The optional component reads GPS status from the configured dish.
Existing write authorization and uncertain-result handling remain in
starlink_client.cpp; a response parsing error never authorizes a retry.

GPS decoding accepts omitted proto3 scalar values as false/0 only inside
present dish/GPS messages. Duplicate message fields merge and the last
scalar value wins. Complete containers, field numbers, wire types, varints
and unary gRPC lengths are validated before publishing status.
Unknown fields with supported wire types are skipped within their bounds.
The parser does not implement deprecated protobuf groups.

Run the actual decoder with generated fixtures and ASAN/UBSAN:
    python3 tests/starlink-gps-native.py --help
    python3 tests/starlink-gps-native.py

These tests send no network commands and do not establish compatibility
with every physical dish firmware. Defaults follow the proto3 reference:
https://protobuf.dev/programming-guides/proto3/#default
