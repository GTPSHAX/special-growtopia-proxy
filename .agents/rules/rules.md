# Project Rules — specialproxy-core

Aturan lintas-session. Sumber: riset docs resmi via Exa (learn.microsoft.com, developer.android.com, doxygen.nl). Verifikasi ulang sebelum mengubah.

## Toolchain per Platform (VERIFIED)

Fakta kunci: **MSVC hanya bisa target Windows.** Kompilator Android resmi dari NDK adalah Clang/LLVM (Microsoft docs: "If you are targeting Android, you can use the Clang/LLVM compiler that ships with the Android NDK"). Workload "Mobile development with C++" dihapus mulai VS 2026.

| Platform | Kompilator | Generator | Catatan |
| --- | --- | --- | --- |
| Windows | MSVC | Ninja / VS | Ninja dari CLI butuh `vcvarsall.bat x64` dulu |
| Linux | GCC/Clang distro | Ninja / Unix Makefiles | — |
| Android | **NDK Clang** | Ninja | Host Windows; triplet via vcpkg |
| MacOS (kedepannya) | Clang | Ninja | Belum aktif |

Dilarang menulis rules/assumsi "MSVC untuk Android" — teknis tidak ada jalurnya.

## Build & Dependency
- CMake ≥ 3.20 (untuk presets dari CLI).
- Vcpkg **manifest mode** (`vcpkg.json` di root). Dependency baru hanya via manifest; dilarang vendoring/unduh manual.
- `CMakePresets.json` checked-in per platform; `CMakeUserPresets.json` untuk path lokal (gitignored).
- Preset: `generator: Ninja`, `architecture.strategy: external`, `toolchainFile: $env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`.
- Jangan commit build artifacts / `build*/`.

## Integrasi Vcpkg + Android
- Env wajib: `VCPKG_ROOT`, `ANDROID_NDK_HOME`.
- `CMAKE_TOOLCHAIN_FILE` = vcpkg.cmake; `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` = `$env{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake`.
- Triplets: `arm64-android` (arm64-v8a), `x64-android` (x86_64), `arm-android`, `arm-neon-android`.
- Semua var yang memengaruhi toolchain/vcpkg di-set **sebelum** `project()` pertama.

## Portabilitas
- Target: Linux, Android, Windows; MacOS kedepannya.
- API platform-specific wajib di balik abstraction layer atau `#ifdef` terlokalisasi; branch platform di CMake dibatasi ke perbedaan link-time.

## Code Style — Doxygen
- Blok: Javadoc style `/** ... */`, tag `@` (bukan `/*!` atau `\param`).
- API public di header wajib terdokumentasi: `@brief` (imperative), `@param` urut deklarasi + `[in]`/`[out]`/`[in,out]`, `@return`, `@tparam` untuk template.
- Trailing comment member: `///<`.
- Deskripsi semantik ("Maximum time to wait, in milliseconds"), bukan restorasi nama param.
- Kode internal trivial boleh tanpa dokumentasi.
