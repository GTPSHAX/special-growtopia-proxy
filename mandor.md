# Mandor — Prinsip Engineering C++ Efisien & Cepat

Panduan kerja untuk specialproxy-core. Sumber riset: C++ Core Guidelines (PER/ES/R/CP), cppreference (multithread, container invalidation), Abseil Tip of the Week, Herb Sutter (Effective Concurrency), learnCpp, codesynthesis (alignment & portability), Microsoft docs (smart pointers), dan referensi protocol Growtopia di `D:\workspace\references\growtopia`.

## 1. Prinsip Inti Performa (Core Guidelines PER)

- PER.1-3: jangan optimasi tanpa alasan, jangan premature, jangan optimasi kode non-kritis. Optimasi hanya jalur panas (packet encode/decode, event loop).
- PER.6: tidak ada klaim performa tanpa pengukuran. Profil dulu, kerjakan sesudahnya.
- PER.10: andalkan static type system. Hindari `void*`, reinterpret_cast berlebih, manipulasi byte manual — type yang jelas lebih mudah dioptimasi compiler.
- PER.11: pindahkan komputasi dari runtime ke compile time (constexpr table, template). Contoh: registry nama variant sebagai constexpr array, bukan unordered_map global dengan init runtime.
- PER.12-13: eliminasi alias & indirection berlebih. Setiap lapisan pointer/reference menghalangi optimizer.
- PER.14: minimalkan alokasi-dealokasi. PER.15: jangan alokasi di jalur kritis.
- PER.16-18: struct kompakt, member paling sering dipakai di depan, space is time. Perf didominasi akses memori, bukan jumlah instruksi.
- PER.19: akses memori prediktabel (sekuensial, cache-friendly). Hindari pointer chasing antar alokasi.
- PER.30: hindari context switch di jalur kritis — event loop jangan spawn thread per paket.

## 2. Aturan Alokasi & Copy

- Pre-`reserve()` ukuran yang diketahui: buffer wire, hasil decode, container. Realokasi vector mahal.
- `emplace_back` > `push_back(tmp)` — konstruksi langsung, tanpa temporary.
- Sink parameter (disimpan ke member): pass **by value + move** — copy elision mengubah copy jadi move gratis (Abseil ToW 117).
- Borrow parameter (hanya dibaca): `std::span`, `std::string_view`, `const&`.
- `string_view` = borrow type. Hanya sebagai parameter/local. Tidak pernah disimpan, tidak pernah dikembalikan dari fungsi (kecuali constexpr global). Tidak menjamin null-terminated — passing ke API C: konversi eksplisit `std::string` dulu.
- Return by value (RVO/NRVO membuat gratis); return `const&` hanya untuk member.
- Alokasi berulang di loop: angkat keluar loop, reuse buffer (clear() mempertahankan capacity).

## 3. Memory Safety (Core Guidelines R, MS Docs)

- RAII untuk semua resource: memori, socket, ENet host/packet, handle. Destructor yang melepas — bukan free manual di banyak jalur.
- Ownership eksplisit:
  - `unique_ptr` = owner tunggal, default untuk semua heap.
  - `shared_ptr` = hanya bila memang multi-owner. Hitung biaya atomic refcount.
  - `weak_ptr` = observer; **wajib** untuk memutus siklus shared_ptr (siklus = leak).
  - Raw pointer = non-owning view saja, scope terbatas, tidak pernah `delete`.
- Buat smart pointer di baris terpisah, bukan dalam argument list (aturan MS docs — mencegah leak halus urutan evaluasi).
- Tidak ada `new`/`delete` langsung. Gunakan `make_unique`/`make_shared` (exception-safe, satu alokasi untuk shared).
- Iterator/reference invalidation (cppreference):
  - `vector`: push_back/insert → realokasi membatalkan SEMUA iterator/ref; erase membatalkan dari titik erase ke belakang. `reserve()` mencegah realokasi.
  - `unordered_map`: rehash membatalkan iterator tapi BUKAN pointer/reference elemen.
  - `map`: insert aman; erase hanya elemen terkait.
  - Aturan praktis: simpan indeks, bukan iterator, bila container berubah di antara pemakaian.

## 4. Concurrency (Core Guidelines CP, cppreference, Sutter)

- Data race = UB (cppreference multithread): akses konflik (satu write) tanpa synchronizes-before. Solusi resmi: atomic, atau happens-before via mutex.
- Sutter: **kaitkan mutex dengan data, bukan dengan kode**. Owner data memegang lock-nya; pemanggil tak perlu tahu. Enkapsulasi: `class DenganLock { mutex m_; Data d_; }` — semua akses `d_` lewat method yang lock `m_`.
- Lock ordering konsisten global — mencegah deadlock. Dua lock = selalu urutan sama.
- Immutability first: data read-only lintas thread tidak butuh lock sama sekali. Pattern snapshot: bangun object baru, publish via atomic pointer swap — reader tidak pernah block writer (cocok untuk config/routing table).
- Value semantics antar thread: pass by value ke job thread = copy, dan seluruh kategori race aliasing hilang. Copy murah dibanding debug race.
- `vector<bool>` TIDAK thread-safe per elemen (bit packing internal).
- Singleton lazy: `std::call_once` atau magic static (C++11 guaranteed thread-safe init).
- Lock-free hanya bila terbukti perlu (profil + benchmark). Atomik dengan memory order non-default = harus ada komentar alasan + bukti.

## 5. Serialisasi Wire Cepat (codesynthesis, alignment)

- Struct packed (`#pragma pack(push,1)`) + `static_assert(sizeof)`: satu `memcpy` = serialisasi tercepat, zero per-field encode. Syarat keabsahan: semua target LE, ukuran type konsisten, struct `#pragma pack` (alignment-compatible) — terpenuhi untuk Windows MSVC + Android NDK Clang.
- Jangan memcpy struct yang TIDAK packed (padding antar platform bisa beda) atau yang berisi pointer/std::string/std::vector.
- Akses unaligned: beberapa ARM trap/slow — pastikan struct packed (alignment 1) sehingga copy via memcpy selalu aman, jangan cast pointer ke dalam buffer mentah.
- Endianness: target kita semua LE — dokumentasikan asumsi ini di satu tempat; bila nanti ada target BE, ganti ke explicit shift encode (satu titik: ByteStream).
- Bounds check di DECODE selalu (input = hostile): panjang minimum, panjang field vs sisa buffer, batas ukuran deklarasi. Encode boleh tanpa check (input terkontrol).
- Validasi deklarasi panjang sebelum alokasi (buffer sizing dari wire = vektor alokasi berbahaya).

## 6. Penerapan di specialproxy-core

- ByteStream: satu utility encode/decode LE bounds-checked — semua modul pakai ini, tidak ada memcpy manual tersebar.
- GameUpdatePacket: `#pragma pack` + `static_assert(sizeof == 56)` — wire serialize = memcpy.
- Limits untuk ukuran wire (max wire packet, max extra) — decode menolak lebih dulu sebelum alokasi.
- Net.hpp: RAII penuh (Host destroy di destructor), move-only (tidak ada double-destroy).
- Registry constexpr (bukan static unordered_map dengan init order issue).
- Jika nanti multithread: event loop single-thread + queue message passing antar thread (hindari shared mutable state; snapshot pattern untuk config).

## 7. Checklist Review Kode (cepat)

1. Ada alokasi di hot path yang bisa di-reserve/di-lift?
2. Parameter sink pakai by-value+move? Borrow pakai span/view?
3. Ownership jelas per resource? Ada raw `new`/`delete`?
4. shared_ptr ada siklus? Perlu weak_ptr?
5. Data lintas thread ada lock/atomic/imutable? Mutex terkait data, bukan prosedur?
6. Decode sudah bounds-check semua field sebelum alokasi?
7. Struct wire packed + static_assert size?
8. Iterator disimpan lintas mutasi container?

## 8. Referensi Protocol Growtopia

Lokasi `D:\workspace\references\growtopia\` dipakai sebagai sumber oracle untuk format wire dan perilaku klien. Pakai referensi ini sebelum menebak struktur packet atau konstanta.

- `Growlauncher-Documentation\` — dokumentasi enum packet types, packet variants, network functions, dan struktur data. Cocok untuk konstanta dan daftar nama fungsi `On*`.
- `growtopia-decompiled\` — source dekompilasi beserta `docs\packet-types\` dan `docs\packet-variants\`. Dipakai untuk memverifikasi layout wire, integrity new-header, dan format konten varian seperti `OnDialogRequest`.

Catatan verifikasi: tabel enum pada biner dekompilasi bisa bergeser dari nilai wire yang sebenarnya. Prioritaskan format yang teramati dari traffic atau sumber proxy referensi bila keduanya bertentangan.
