# Project Rules — specialproxy-core

Aturan lintas-session. Sumber: riset docs resmi via Exa (learn.microsoft.com, developer.android.com, doxygen.nl). Verifikasi ulang sebelum mengubah.

## Build Matrix (semua cross-build dari Windows host)

| Preset | Target | Kompilator | Triplet vcpkg | Status |
| --- | --- | --- | --- | --- |
| `windows-x64-msvc` | Windows x64 | MSVC | `x64-windows` | ✅ verified |
| `windows-x86-msvc` | Windows x86 | MSVC | `x86-windows` | ✅ build + test verified |
| `android-arm64` | arm64-v8a | NDK Clang | `arm64-android` | ✅ build verified |
| `android-armeabi` | armeabi-v7a | NDK Clang | `arm-neon-android` | ✅ build verified |
| `linux-x64-cross` | Linux x64 | Clang + sysroot | `x64-linux` | butuh LLVM + `LINUX_SYSROOT` |
| `macos-x64-cross` | macOS x64 | Clang + macOS SDK | `x64-osx` | butuh LLVM + `MACOS_SDK_PATH` |
| `macos-arm64-cross` | macOS arm64 | Clang + macOS SDK | `arm64-osx` | butuh LLVM + `MACOS_SDK_PATH` |

Fakta kunci: **MSVC hanya bisa target Windows.** Android = NDK Clang. Linux/macOS dari Windows = Clang + sysroot/SDK (macOS SDK wajib disalin dari Mac, lihat osxcross/WinToMacApps).

## Env Requirements
- `VCPKG_ROOT` — semua preset.
- `ANDROID_NDK_HOME` — preset android.
- LLVM/Clang di PATH — preset linux/macos cross.
- `LINUX_SYSROOT` — linux-x64-cross.
- `MACOS_SDK_PATH` — preset macos.
- MSVC Ninja build dari CLI: `vcvarsall.bat x64` (atau `x86` untuk preset x86) sebelum cmake.

## Build & Dependency
- CMake ≥ 3.20 (terpasang 4.4.3), Ninja generator untuk semua preset.
- Vcpkg **manifest mode** (`vcpkg.json` di root). Dependency baru hanya via manifest; dilarang vendoring/unduh manual.
- `CMakePresets.json` checked-in per platform; `CMakeUserPresets.json` untuk path lokal (gitignored).
- Jangan commit build artifacts / `build*/`.
- Toolchain cross custom ada di `cmake/toolchains/` (linux.cmake, macos.cmake); Android chainload langsung ke NDK.

## Integrasi Vcpkg + Android
- `CMAKE_TOOLCHAIN_FILE` = vcpkg.cmake; `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` = NDK `android.toolchain.cmake` (atau toolchain custom).
- Triplets Android terpakai: `arm64-android` (arm64-v8a), `arm-neon-android` (armeabi-v7a, NEON ON), `x64-android` (x86_64).
- **Dilarang pakai triplet `arm-android`** — NEON OFF ditolak NDK r26+ ("Disabling Neon is no longer supported").
- Semua var yang memengaruhi toolchain/vcpkg di-set **sebelum** `project()` pertama.

## Portabilitas
- Target: Windows x64/x86, Android arm64/armeabi, Linux x64, macOS x64/arm64.
- API platform-specific wajib di balik abstraction layer atau `#ifdef` terlokalisasi; branch platform di CMake dibatasi ke perbedaan link-time.

## CI (.github/workflows/)
- 4 workflow di main: windows.yml (x64+x86, build+ctest), linux.yml (build+ctest), android.yml (build-only), macos.yml (native arm64, build+ctest).
- vcpkg CI checkout di-pin ke commit baseline manifest (`df25fb4`) — update pin ini saat bump `builtin-baseline`.
- `VCPKG_DEFAULT_BINARY_CACHE` = `vcpkg-bincache/` — dir harus dibuat manual sebelum configure (cache miss tidak membuat dir).
- Windows CI env MSVC via `ilammy/msvc-dev-cmd@v1`.

## Code Style — Doxygen
- Nama file: **PascalCase** wajib (header/impl/test: `TankPacket.hpp`, `Net.cpp`, `TestPackets.cpp`).
- Komentar inline wajib netral & informatif, tanpa slang/marker aneh ("ponytail", "TODO santai", dsb.).
- Komentar inline **dilarang** memakai em-dash, emoji, simbol dekoratif, maupun tanda `:` dan `;`.
- Komentar ditujukan untuk kode, bukan untuk orang — hanya menjelaskan alasan adanya hal yang tidak lazim. Bila kode sudah bisa dinalar dan logikanya terbaca, jangan tambah komentar.
- Penulisan kode dan komentar wajib bahasa Inggris (identifier, pesan log, isi komentar).
- Blok: Javadoc style `/** ... */`, tag `@` (bukan `/*!` atau `\param`).
- API public di header wajib terdokumentasi: `@brief` (imperative), `@param` urut deklarasi + `[in]`/`[out]`/`[in,out]`, `@return`, `@tparam` untuk template.
- Trailing comment member: `///<`.
- Deskripsi semantik ("Maximum time to wait, in milliseconds"), bukan restorasi nama param.
- Kode internal trivial boleh tanpa dokumentasi.
