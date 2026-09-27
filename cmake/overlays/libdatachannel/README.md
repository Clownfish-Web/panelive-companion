# libdatachannel input limits

This overlay pins libdatachannel 0.24.5 and its upstream archive hash in `portfile.cmake`.
Port revision 2 includes all patches needed to build the dependency with this application.

`sctp-message-limit.patch` bounds incoming message reassembly before appending fragments.
`sctp-ppid-policy.patch` rejects unsupported SCTP payload types and removes deprecated
partial-message handling. The remaining patches preserve the baseline vcpkg integration.

The patches modify MPL-2.0-covered `src/impl/sctptransport.cpp` and `.hpp`.
Provide the pinned upstream source and these patches to binary recipients as corresponding
source. Keep this overlay when building on every supported platform.
