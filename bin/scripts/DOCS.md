# Lua API Reference

Dokumentasi API custom yang tersedia di Lua engine Special Proxy Core. API ini didaftarkan oleh `src/script/LuaEngine.cpp`; bentuk packet dan tank mengikuti `src/script/LuaPacket.cpp`.

Script dimuat saat proxy berjalan. Gunakan hook untuk mengamati atau memodifikasi packet, command untuk aksi pengguna, dan fungsi global untuk mengirim packet atau membaca state. Contoh di bawah mengasumsikan script berada di folder `bin/scripts/`.

```lua
addHook(function(packet, raw)
  if packet.kind == "text" then
    log("message type=" .. tostring(packet.message_type))
  end
end, "onclientpacket")
```

## Aturan umum

- Lua standard libraries dibuka oleh engine (`luaL_openlibs`). API standar seperti `string`, `table`, `math`, `io`, dan `os` tersedia. Script lokal dipercaya dan tidak disandbox.
- Setiap callback Lua dibatasi 200.000 instruksi. Error callback dicatat dan tidak seharusnya menghentikan proxy.
- Fungsi pengiriman mengembalikan boolean keberhasilan jika disebutkan di bawah. Arah `upstream` berarti ke server; `downstream` berarti ke client game.
- Fungsi `sleep` dan HTTP dapat menunggu. Dalam command coroutine, penantian bersifat kooperatif; di luar coroutine, pemanggilan dapat memblokir thread pemanggil.

## Hook packet dan event

### `addHook(callback, name)`

Daftarkan callback pada event. Bentuk alternatif yang juga diterima: `addHook(name, callback)`. Nama event tidak peka huruf besar/kecil; awalan `on` dihapus. Beberapa callback boleh memakai event yang sama dan semuanya dijalankan.

Nama event yang digunakan: `clientpacket`, `serverpacket`, `variant`, `gamepacket`, `gamemessage`, `connect`, `disconnect`, `redirect`, `inventory`. Alias `sendpacket` dan `sendpacketraw` mengarah ke `clientpacket`.

Packet callback menerima `(packet, raw)`, dengan `raw` berupa frame wire binary. Callback packet boleh mengembalikan:

| Return | Arti |
| --- | --- |
| `nil` atau `true` | Teruskan packet tanpa perubahan. |
| `false` | Drop packet. |
| string binary | Ganti packet dengan frame tersebut. |

Hook untuk event non-packet menerima argumen event yang dijelaskan di tabel berikut; nilai return diabaikan.

### `Event` dan `addEvent(event, callback)`

API Mori memakai konstanta numerik `Event`. Daftarkan dengan `addEvent(Event.connect, function(...) ... end)`. Event yang benar-benar dikirim engine:

| Konstanta | Nilai | Callback | Argumen |
| --- | ---: | --- | --- |
| `Event.clientpacket` | 1 | `onclientpacket` | `(packet, raw)`; C2S sebelum diteruskan. |
| `Event.serverpacket` | 2 | `onserverpacket` | `(packet, raw)`; S2C sebelum hook spesifik. |
| `Event.variant` | 3 | `onvariant` | `(packet, raw)`; variant S2C. |
| `Event.gamepacket` | 4 | `ongamepacket` | `(packet, raw)`; game packet S2C. |
| `Event.gamemessage` | 5 | `ongamemessage` | `(session, body, "")`; text S2C. |
| `Event.connect` | 6 | `onconnect` | `(session, "", "")`. |
| `Event.disconnect` | 7 | `ondisconnect` | `(session, "", "")`. |
| `Event.redirect` | 8 | `onredirect` | `(session, address, port)`; `port` berupa string. |
| `Event.command` | 9 | — | Konstanta disediakan, tetapi engine saat ini tidak mengirim event ini. |
| `Event.inventory` | 10 | `oninventory` | `(session, changes)` saat slot inventory berubah. |

Urutan S2C: `onserverpacket` lebih dahulu, kemudian `onvariant`, `ongamepacket`, atau `ongamemessage`. Mengembalikan `false`/frame pengganti dari hook packet menghentikan atau mengganti forwarding packet tersebut.

### Registrasi dan kompatibilitas hook

- `AddHookCallback`: alias `addHook`.
- `removeHook(name)` dan `RemoveHookCallback(name)`: hapus semua callback terdaftar untuk nama event itu. Alias dan prefiks `on` dinormalisasi seperti pada `addHook`.
- `removeAllHooks()`: hapus semua hook. `removeEvents()` adalah aliasnya; command yang didaftarkan dengan `registerCommand` tidak terhapus.
- `CallHookCallback(name, ...)`: jalankan callback pertama yang terdaftar pada event dengan argumen yang diberikan. Mengembalikan `true` bila callback ada dan selesai tanpa error, selainnya `false`.
- `applyHook(...)`: saat ini no-op yang selalu mengembalikan `true`.
- `listenEvents(...)` dan `unlistenEvents(...)`: kompatibilitas API; saat ini no-op yang selalu mengembalikan `false`.

### Bentuk `packet`

Semua packet callback menerima table berikut. Field yang tidak relevan untuk jenis packet bisa tidak ada.

| Field | Isi |
| --- | --- |
| `data` | Frame wire lengkap sebagai binary string. |
| `size` | Panjang frame dalam byte. |
| `kind` | `"text"`, `"game"`, `"variant"`, atau `"raw"` bila decode gagal/tidak dikenal. |
| `message_type` | Jenis message wire. |
| `session` | ID session proxy. |
| `body` | Isi text packet. |
| `name` | Nama fungsi variant. |
| `params` | Parameter variant kompatibilitas; indeks Lua mulai 1, nilai yang dapat diubah ke string. |
| `values` | Array rapat nilai variant bertipe, setiap entri `{index, type, value}`. `index` adalah indeks wire mulai 0; slot 0 berisi nama fungsi. |
| `tank` | Header tank untuk `game` dan `variant`. |
| `raw()` | Mengembalikan `data` yang sama. |

Tipe pada `values`: `string`, `float`, `vec2`, `vec3`, `uint32`, `int32`. Vector adalah table `{x=..., y=...}` atau `{x=..., y=..., z=...}`. Field variant `params` tidak membawa vector.

### Contoh hook

```lua
addHook(function(packet, raw)
  if packet.kind == "variant" then
    log(packet.name .. " frame=" .. tostring(#raw))
  end
end, "onserverpacket")

addEvent(Event.connect, function(session, address, port)
  log("connected session=" .. tostring(session))
end)

addEvent(Event.inventory, function(session, changes)
  for _, change in ipairs(changes) do
    log("slot=" .. change.slot .. " " .. change.oldCount .. " -> " .. change.newCount)
  end
end)
```

## Command chat

### `registerCommand(name, callback)` / `registerCommand(name, description, callback)`

Daftarkan command chat; `addCommand` adalah alias. Nama boleh diawali `/` dan dinormalisasi menjadi huruf kecil. Callback menerima setiap argumen command sebagai string positional, bukan sebagai table. Contoh `/warp WORLD` memanggil `function(world) ... end`. Registrasi Lua dengan nama sama menggantikan callback Lua sebelumnya.

Command yang dikenal dikonsumsi oleh proxy; command tak dikenal diteruskan ke server. Command callback dapat memakai fungsi API lain, termasuk `sleep` dan `fetch`.

```lua
registerCommand("/hello", "Say hello", function(name)
  send_log("Hello " .. (name or "there"))
end)
```

### `requestScriptReload()`

Minta hot reload script di poll proxy berikutnya. Mengembalikan `true` bila permintaan diterima, `false` bila engine tidak tersedia. Reload memuat ulang semua file `.lua` di direktori script yang dikonfigurasi; state lokal, hook, command, dan timer script dibuat ulang.

```lua
registerCommand("/reload", function()
  if not requestScriptReload() then warn("reload request failed") end
end)
```

## Logging dan informasi proxy

| API | Hasil / efek |
| --- | --- |
| `log(text)` | Tulis pesan level info ke log proxy. |
| `warn(text)` | Tulis pesan level warning ke log proxy. |
| `version()` | String versi core. |
| `session_id()` | ID session callback aktif; `0` di luar callback. |
| `client_count()` | Jumlah client aktif; `0` di luar callback. |
| `upstream()` | Dua hasil: `address, port`; di luar callback `"", 0`. |
| `getBot()` | Table facade dengan method yang didokumentasikan berikut. |

`getBot()` menyediakan `sendPacket(type, text)`, `sendRaw(tank)`, `say(text)`, `upstream()`, `session_id()`, `client_count()`. Pemanggilan yang aman: `bot:sendPacket(...)`, `bot:sendRaw(...)`, `bot:upstream()`, `bot:session_id()`, `bot:client_count()`. `say` tidak menerima argumen `self`; gunakan `bot.say("hello")` atau global `send_chat("hello")`.

## Mengirim packet

### Text dan frame mentah

| API | Arah dan parameter | Hasil |
| --- | --- | --- |
| `send_to_server(frame[, reliable])` | Kirim frame wire binary ke upstream. `reliable` default `true`. | Boolean. |
| `send_to_client(frame[, reliable])` | Kirim frame wire binary ke downstream. `reliable` default `true`. | Boolean. |
| `sendPacket(type, text)` | Text packet ke upstream; `type` adalah message type wire. | Boolean. |
| `sendPacketToClient(type, text)` | Text packet ke downstream. | Boolean. |
| `send_chat(text)` / `chat(text)` | Chat ke upstream; membentuk type 2 dan body `action|input\n|text|...`. | Boolean. |
| `send_console(text)` | Kirim `OnConsoleMessage` ke client. | Boolean. |
| `send_log(text)` | Kirim action log ke client. | Boolean. |

Contoh mengirim text generik:

```lua
sendPacket(3, "action|quit_to_exit")
send_chat("Hello")
send_log("Pesan untuk client")
```

Raw frame dikirim tanpa encoding ulang. Gunakan `packet.raw()` atau argumen `raw` dari hook jika mengirim frame yang sudah ada.

### Tank/game packet

- `GameUpdatePacket.new()`: buat header tank baru dengan field bernilai nol.
- `sendRaw(tank)` dan alias `sendPacketRaw(tank)`: serialisasi tank lalu kirim ke upstream.
- `sendRawToClient(tank)`: serialisasi tank lalu kirim ke downstream.
- `sendVariant(name, value1, ..., value7)`: kirim variant ke downstream. String, integer, dan number diterima; integer ditulis sebagai `int32`, number non-integer sebagai `float`. Tipe vector belum didukung fungsi ini.

Field yang dapat dibaca dan ditulis pada userdata tank: `type`, `net_id`, `item_net_id`, `flags`, `float_var`, `int_data`, `pos_x`, `pos_y`, `pos_x2`, `pos_y2`, `particle_time`, `int_x`, `int_y`, `data_size`. Gunakan ejaan persis ini.

```lua
local tank = GameUpdatePacket.new()
tank.type = 3
tank.int_data = 4992
tank.int_x = 10
tank.int_y = 20
sendRawToClient(tank)

sendVariant("OnConsoleMessage", "Hello from Lua")
```

### Decode frame

`decode(frame)` atau alias `Packet.decode(frame)` mengurai frame binary dan mengembalikan table `packet` yang sama bentuknya dengan hook. Frame invalid menghasilkan table dengan `kind == "raw"`; argumen non-string menghasilkan `nil`.

```lua
local parsed = decode(raw_frame)
if parsed and parsed.kind == "text" then
  log(parsed.body)
end
```

## Packet dialog

### `blockDialog(pattern[, count])`

Daftarkan ECMAScript regex untuk body `OnDialogRequest`. Mengembalikan boolean validitas regex. Match memakai pencarian di bagian mana pun dari body. `count` default `1`; nilai positif memblokir sejumlah match, nilai negatif memblokir tanpa batas, `0` tidak menambah blok. Registrasi pola sama menambah jumlah sisa; nilai negatif membuatnya tanpa batas.

Dialog yang diblokir tidak diteruskan ke client. Core mengirim state player ke upstream untuk menjaga state sinkron.

```lua
blockDialog("drop_item", 3) -- blok tiga dialog yang cocok
blockDialog("purchase|confirm", -1) -- blok terus-menerus
```

## Player, world, inventory, item

Data ini berasal dari packet server yang telah diamati. Nilai dapat kosong sebelum state diterima atau saat area tidak ter-track.

### Player

- `getPlayerList()`: array player.
- `getPlayerByNetID(netID)`: player atau `nil`.
- `getLocal()`: player pertama yang ter-track, atau `nil`; bukan jaminan identitas lokal yang otoritatif.

Field player: `{netID, userID, name, country, posX, posY, isLeft}`.

### World dan tile

- `getWorldName()` / `getCurrentWorldName()`: nama world.
- `getWorldTileMap()`: `{name, width, height, count}`; `count` adalah jumlah tile pada metadata world.
- `getTile(x, y)`: tile atau `nil`.
- `getTiles()`: seluruh tile yang ter-track.

Field tile: `{x, y, fg, bg, flags, fgName, bgName}`; `fg` dan `bg` adalah ID item foreground/background.

### Object dan NPC

- `getObjectList()`: array `{id, itemid, x, y, amount, name}`.
- `getNPCList()`: array `{netID, id, x, y, flags}`.

### Inventory

- `getInventory()`: item terisi sebagai `{id, count, flags, name}`.
- `getInventorySlots()`: seluruh slot termasuk kosong sebagai `{id, count, flags, name, empty}`. Slot kosong memakai `id == 0`, `empty == true`.
- `setInventorySlot(slot, id, count)`: kirim delta remove/add ke client untuk isi slot 1-based; `count` harus 0–200. ID 0 berarti kosong. Mengembalikan boolean pengiriman. State inventory lokal diperbarui berdasarkan delta item; ini bukan perubahan database server.

```lua
for index, slot in ipairs(getInventorySlots()) do
  if not slot.empty then
    log(index .. ": " .. slot.name .. " x" .. slot.count)
  end
end
setInventorySlot(1, 242, 10)
```

Event `Event.inventory` menyertakan array `changes`. Tiap entri: `{slot, oldId, oldCount, newId, newCount, name, oldName, empty}`. Nama merujuk ID baru dan `oldName` ID lama. Slot 1-based.

### Item database dan `items.dat`

- `getItemName(id)`: nama item atau `nil` jika belum ada/tidak ditemukan.
- `getItemInfo(id)`: `nil` jika tidak ditemukan, selain itu `{id, name, type, material, rarity, maxAmount, flags, untradeable}`. `untradeable` berasal dari bit `0x8000` pada `flags`.
- `getItemsDat()`: dua hasil. Saat tersedia `(bytes, nil)`, dengan `bytes` raw binary `items.dat` hasil dekompresi; sebelum diterima `(nil, "items.dat not received")`.

Header raw `items.dat` dimulai dengan version pada byte pertama; jumlah item adalah byte ke-3 dan ke-4 little-endian:

```lua
local data, err = getItemsDat()
if data then
  local version = data:byte(1)
  local countLow, countHigh = data:byte(3, 4)
  local count = countLow + countHigh * 256
  log("items.dat version=" .. version .. " count=" .. count)
else
  warn(err)
end
```

## Device info

`getDeviceInfo()` mengembalikan `{hostname, os, platform, arch, cpuCount}`. `platform` berupa label OS seperti `windows`, `linux`, `macos`, atau `android`.

## Timer dan encoding

- `sleep(milliseconds)` dan alias `sleepMs(milliseconds)`: tunggu durasi positif. Dalam coroutine command, yield kooperatif; di callback non-yieldable dapat memblokir.
- `scheduleCallback(callback, delay_ms)`: jadwalkan callback sekali setelah delay (ms), dengan host/session pemanggil. Mengembalikan `true` bila terjadwal, `false` bila tidak tersedia. Callback berjalan pada poll proxy; gunakan untuk menunda kerja keluar dari packet hook.
- `base64Encode(binary_string)`: encode byte string menjadi Base64 standar.

```lua
addHook(function(packet)
  if packet.kind == "game" and packet.tank.type == 16 then
    scheduleCallback(function()
      log("items packet observed")
    end, 10)
  end
end, "ongamepacket")
```

## HTTP client

### `fetch(url[, options])`

Lakukan HTTP/HTTPS request. Return table `{ok, status, body, error, headers}`. `headers` adalah array `{name, value}`. Untuk respons HTTP seperti 404, `status` tetap berisi status HTTP; cek `ok` dan `status`.

| Opsi | Tipe | Default / arti |
| --- | --- | --- |
| `method` | string | `GET`. |
| `headers` | table string-ke-string | Header request tambahan. |
| `body` | string | Body request, termasuk binary string. |
| `timeout` | integer | 10000 ms; nilai positif. |
| `maxBytes` | integer | 8 MiB maksimum body response; nilai positif. |
| `delay` | integer | Delay awal opsional dalam ms. |
| `socks5` | boolean | Override pemakaian SOCKS5 untuk request ini; tanpa opsi, ikuti konfigurasi HTTP Lua. |

Di command coroutine, request dijalankan scheduler dan callback dilanjutkan saat hasil siap. Di luar coroutine request berjalan sinkron dan menahan pemanggil sampai selesai.

```lua
local response = fetch("https://example.com/api", {
  method = "POST",
  headers = { ["Content-Type"] = "application/json" },
  body = '{"ok":true}',
  timeout = 5000,
  maxBytes = 1024 * 1024
})
if response.ok then
  log("HTTP " .. response.status .. " bytes=" .. #response.body)
else
  warn("HTTP failed: " .. response.error)
end
```

TLS certificate verification saat ini dinonaktifkan pada HTTP client. Jangan menganggap validasi sertifikat melindungi request HTTPS.

### `Http.new([defaults])`

Buat client dengan opsi default yang sama seperti `fetch`. Method tersedia:

- `client:get(url[, options])`: paksa method GET.
- `client:post(url[, options])`: paksa method POST.
- `client:request(url[, options])`: method mengikuti `options.method` atau GET default.

Method mendukung bentuk colon maupun dot. Opsi request menggabungkan default client dan opsi lokal; opsi lokal mengoverride nilai yang sama.

```lua
local http = Http.new({timeout = 4000, headers = {Accept = "application/json"}})
local response = http:get("https://example.com/status")
```

### SOCKS5 untuk HTTP Lua

- `setHttpSocks5(enabled)`: set boolean default routing, mengembalikan status boolean.
- `setHttpSocks5Config(host, port[, username, password])`: set proxy SOCKS5 untuk HTTP Lua. Port harus 1–65535; kredensial opsional. Mengembalikan `false` bila konfigurasi invalid, `true` bila diterapkan.
- Konfigurasi proxy yang diterima core diwariskan sebagai default. Opsi request `socks5 = false` dapat menonaktifkannya untuk satu request.

```lua
if not setHttpSocks5Config("127.0.0.1", 1080) then
  warn("invalid SOCKS5 configuration")
end
```

Jangan menulis username/password proxy ke log atau dokumentasi publik.

## Ringkasan alias

| Nama utama | Alias |
| --- | --- |
| `addHook` | `AddHookCallback` |
| `removeHook` | `RemoveHookCallback` |
| `removeAllHooks` | `removeEvents` |
| `send_chat` | `chat` |
| `sendRaw` | `sendPacketRaw` |
| `getWorldName` | `getCurrentWorldName` |
| `sleep` | `sleepMs` |
| `registerCommand` | `addCommand` |
| `decode` | `Packet.decode` |
