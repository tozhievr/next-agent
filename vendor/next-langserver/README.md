# Embedded next-langserver

The `source/` snapshot contains upstream engine, host and ABI headers from
`D:/next-langserver`. NextAgent's CMake builds the host/engine libraries into its
C++26 core. The standalone LSP frontend is neither built nor packaged.

`windows-x64/plugins/` contains all 30 plugin DLLs with their manifests and query
resources from the upstream `dist/lxserver/plugins` bundle. Plugin loading stays
inside the NextAgent process and is selected by the requested file extension.
The DLLs were rebuilt from their generated grammar projects with Clang 23.1.3,
Release optimization and `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`. Their only
imported DLL is KERNEL32.dll; no debug or redistributable VC runtime is required.

The former standalone runtime executable and interim bridge DLL are not used.
`linux-x64/plugins/` contains the same 30 languages built as ELF .so libraries
with Linux Clang 23.1.6 in WSL2 Ubuntu 26.04. Both snapshots include the query
resources. `tools/build_plugins.py` rebuilds either platform from the sibling
next-langserver generated grammar projects.

The vendored host has a downstream Linux loader implementation using
`dlopen(RTLD_NOW | RTLD_LOCAL)`, `dlsym`, and `dlclose`, with the same ABI and
manifest/hash checks as the Windows loader. Preserve this adaptation when
refreshing the upstream source snapshot.

Source commit: `af82c507fa0f8774ec7058a3c57360db27396c82` (local working tree may include changes).
