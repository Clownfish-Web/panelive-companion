# CivetWeb input validation

This overlay pins CivetWeb 1.16 and its upstream archive hash in `portfile.cmake`.
Port revision 9 retains the MIT license and baseline vcpkg packaging patches.

The additional patches enforce HTTP request framing and target validation, bound HTTP
and WebSocket reads with monotonic deadlines, validate WebSocket frame headers before
reading payloads, and reject frames larger than 1,048,576 bytes before allocation.
The server-close patch terminates connections after a server Close write attempt.

Keep all patches and this overlay when building on every supported platform.
