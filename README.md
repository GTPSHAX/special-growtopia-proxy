# specialproxy-core

> Cross-platform C++20 core library for a Growtopia UDP proxy, with Lua scripting, packet hooks, world and inventory state, SOCKS5 routing, and an optional HTTPS bootstrap service.

## Features

- Shared library with public C++ API for starting/stopping the proxy and managing upstream connections.
- Packet decode, interception, forwarding, and runtime hooks.
- Lua API for commands, packet manipulation, player/world/inventory queries, timers, and HTTP requests. See [`bin/scripts/DOCS.md`](bin/scripts/DOCS.md).
- SOCKS5 support for upstream game traffic and HTTP requests.
- Optional HTTPS bootstrap service for server-data discovery and upstream updates.
- Windows, Android, Linux, and macOS build presets.

## Requirements

- [CMake](https://cmake.org/) 3.20 or later
- [Ninja](https://ninja-build.org/)
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set to its installation directory
- A platform compiler and toolchain:
  - Windows: Visual Studio C++ tools; initialize the matching environment with `vcvarsall.bat x64` or `vcvarsall.bat x86`
  - Android: Android NDK; set `ANDROID_NDK_HOME`
  - Linux cross-build from Windows: LLVM/Clang and a Linux sysroot in `LINUX_SYSROOT`
  - macOS cross-build from Windows: LLVM/Clang and a copied macOS SDK in `MACOS_SDK_PATH`

Dependencies are managed through the root `vcpkg.json` manifest. CMake presets use Ninja and create build trees under `build/<preset>`.

## Build and test

From a Visual Studio developer command prompt, build Windows x64:

```bat
cmake --preset windows-x64-msvc
cmake --build --preset windows-x64-msvc --parallel
ctest --preset windows-x64-msvc
```

Or use the helper script:

```bat
scripts\build.bat windows-x64-msvc --test
```

`scripts/build.bat` defaults to `windows-x64-msvc`. Supported helper presets:

| Preset | Target |
| --- | --- |
| `windows-x64-msvc` | Windows x64 |
| `windows-x86-msvc` | Windows x86 |
| `android-arm64` | Android `arm64-v8a` |
| `android-armeabi` | Android `armeabi-v7a` with NEON |
| `linux-x64-cross` | Linux x64 cross-build from Windows |
| `macos-x64-cross` | macOS x64 cross-build from Windows |
| `macos-arm64-cross` | macOS arm64 cross-build from Windows |

Run `scripts\build.bat --help` for options. `--test` is available for Windows presets; checked-in CTest presets also support native `linux-x64` and `macos-arm64` configurations.

The project enables tests and examples by default. To configure a library-only build, pass `-DSPXCORE_BUILD_TESTS=OFF -DSPXCORE_BUILD_EXAMPLES=OFF` at configure time.

## C++ integration

The core target is `specialproxy_core`. Public headers are under `include/specialproxy/`. A minimal consumer can start the proxy with `ProxyConfig`:

```cpp
#include "specialproxy/proxy/Core.hpp"

auto start_proxy() -> bool {
  spx::proxy::ProxyConfig config;
  config.listen_port = 17099;
  config.server_address = "127.0.0.1";
  config.server_port = 17091;

  return spx::proxy_start(config);
}
```

The proxy starts on a background thread. Call `proxy_stop()` during application shutdown. `proxy_toggle("verbose", true)` enables per-packet logging; `proxy_message_clients(text)` sends a console message to connected clients. For runtime upstream updates, use `proxy_set_upstream(address, port)`; existing sessions retain their current target, new sessions use the updated endpoint.

### Lua scripts

Set the script directory before starting the proxy. By default, the core loads `.lua` files from `scripts/` relative to the process working directory:

```cpp
#include "specialproxy/proxy/Core.hpp"
#include "specialproxy/script/Script.hpp"

int main() {
  spx::script_set_directory("bin/scripts");

  spx::proxy::ProxyConfig config;
  if (!spx::proxy_start(config)) {
    return 1;
  }

  // Run the host application's event loop here.
  spx::proxy_stop();
}
```

Alternatively, queue a Lua source string with `spx::script_load()` or a file with `spx::script_load_file()` before `proxy_start()`. Scripts are trusted local code and are not sandboxed.

## Local proxy example

The `CoreTest` example builds an interactive proxy, fake server, and fake client for local end-to-end testing. From `build/windows-x64-msvc/bin`, start each executable in a separate terminal:

```text
CoreTestServer.exe
CoreTestProxy.exe
CoreTestClient.exe
```

The fake server listens on UDP `17091`, the proxy on `17099`, and its loopback upstream-control channel on `17098`. Type messages into `CoreTestClient`; use `quit` in `CoreTestProxy` to stop it. The proxy console also supports `clients <text>` and `toggle verbose`.

The proxy example accepts an optional upstream address and SOCKS5 settings:

```text
CoreTestProxy.exe 192.0.2.10 --socks5 127.0.0.1:1080
```

### Optional HTTPS bootstrap example

`CoreTestHttps.exe` is a separate process that serves HTTPS bootstrap traffic and sends discovered upstream endpoints to `CoreTestProxy` through its loopback control channel. Default ports are proxy UDP `17099`, control UDP `17098`, and HTTPS TCP `443`.

```text
CoreTestHttps.exe --no-install 17099 17098 443
```

By default, the HTTPS service can install host mappings and a CA certificate into system locations; this requires elevated privileges. Use `--no-install` to skip those system changes during local tests. SOCKS5 can be set with `--socks5 host:port[:user:pass]`.

## Platform notes

- MSVC presets require the matching target architecture environment; MSVC does not cross-compile to Android, Linux, or macOS.
- Android uses NDK Clang and the `arm64-android` / `arm-neon-android` vcpkg triplets.
- Linux/macOS cross-builds from Windows require their external sysroot or SDK; native presets are also defined for Linux x64 and macOS arm64.
- The HTTPS example is not built for Android.

## Contributing

- Follow the project conventions in [`AGENTS.md`](AGENTS.md).
- Run the relevant build and CTest preset before submitting changes.
- Keep dependency changes in `vcpkg.json`; do not vendor packages manually.
