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

## Logging (wajib)
- Setiap fitur atau perubahan behavior wajib punya log debug yang cukup untuk membuktikan jalur kode mana yang benar-benar jalan saat runtime.
- Log harus menunjukkan keputusan penting dan datanya, bukan cuma "start"/"stop". Contoh wajib: alamat dan port yang dipakai, hasil DNS, nilai field yang di-intercept, nilai sebelum dan sesudah diubah, alasan kegagalan, dan thread/panggilan yang relevan.
- Gunakan modul `spx::log` (`specialproxy/utils/Logger.hpp`) untuk semua log di `src/`, bukan `std::printf` langsung. Prefix konsisten per modul, misalnya `proxy`, `https`, `dns`, `server_data`. Semua log dalam bahasa Inggris.
- Jangan mengandalkan verbose flag untuk informasi diagnostik inti. Log yang dibutuhkan untuk memahami alur harus tetap terlihat pada level default. Verbose hanya untuk detail per paket.
- Jangan pernah menambahkan log yang membocorkan kredensial, token, cookie, atau data akun. Tampilkan hanya nama field, panjang, atau nilai yang bukan rahasia.

## Lua Script Location
- Runtime scripts supplied or edited by the user belong in `bin/scripts/`. Do not relocate them to the repository-root `scripts/` directory; the user manages ignore rules for `bin/` artifacts.

## Documentation Writing
- Before drafting, identify the document's purpose, audience, genre, tone, and evidence needed. Narrow the scope to the reader's actual question.
- Open with the concrete context or problem and why it matters to this reader. Avoid generic throat-clearing, forced hooks, clichés, and unsupported claims.
- Organize around the reader's task. Keep each paragraph focused on one idea; support claims with specific examples, evidence, or steps, then explain their relevance. Use transitions where they clarify sequence or relationships.
- Draft for completeness first; revise structure, focus, clarity, and word choice before proofreading. Read aloud when useful. Prefer direct, specific wording over inflated language.
- For technical documentation, prioritize accurate, verifiable instructions and examples over narrative flourish. Include only background needed to act safely and successfully.
- Research basis: Purdue OWL, “The Writing Process” (https://owl.purdue.edu/owl/resources/teaching_resources/the_writing_process-20260419.pdf) and “Prewriting” (https://owl.purdue.edu/owl/general_writing/the_writing_process/prewriting/index.html); Harvard College Writing Center, “Introductions” (https://writingcenter.fas.harvard.edu/introductions) and “Anatomy of a Body Paragraph” (https://writingcenter.fas.harvard.edu/anatomy-body-paragraph); UNC Writing Center, “Paragraphs” (https://writingcenter.unc.edu/tips-and-tools/paragraphs/). Apply these as practical guidance, not rigid templates.
