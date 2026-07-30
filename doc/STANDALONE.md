# x-unikey standalone — kiến trúc để chạy như fcitx5-unikey

Mục tiêu: gõ tiếng Việt trên desktop hiện đại (Wayland + X11) mà **không cần ibus hay
fcitx5**, dùng lại nguyên bộ máy `libukint`/`ukengine` đã có trong repo này.

## 1. Sự thật kỹ thuật (đã kiểm chứng trên máy này, 2026-07-30)

```
libmutter-18 export:   zwp_text_input_manager_v3, zwp_text_input_v3
                       KHÔNG có zwp_input_method_v2
gtk-3.0/immodules/:    im-fcitx5.so, im-ibus.so, im-wayland.so, im-xim.so
gtk-4.0/immodules/:    libim-fcitx5.so, libim-ibus.so
qt{5,6}/platforminputcontexts/: libfcitx5..., libibus..., libcompose...
```

Hệ quả — đây là điều quyết định toàn bộ thiết kế:

- **Không có đường chính thống để IME cắm vào GNOME/Mutter.** `zwp_input_method_v2` (giao
  thức để IME phục vụ app qua compositor) chỉ có trên wlroots — sway, Hyprland. Mutter
  không implement nó, và chỉ nối `text_input_v3` vào ibus tích hợp sẵn của gnome-shell.
- **Cách fcitx5 chạy được trên GNOME là đi vòng qua compositor**: nó ship module riêng cho
  từng toolkit (`fcitx5-frontend-gtk3`, `-gtk4`, `-qt5`, `-qt6`), các module này nói chuyện
  thẳng với daemon qua D-Bus. Ta sao chép đúng mô hình đó — và cả 3 toolkit đều còn cho
  phép nạp module bên thứ ba.
- **Giới hạn không vượt qua được**: app dùng `text_input_v3` trực tiếp (Electron/Chromium →
  VSCode, và app GTK4 khi chạy backend Wayland mặc định) trên GNOME chỉ nói chuyện được với
  ibus. **fcitx5 cũng không gõ được vào VSCode trên GNOME Wayland.** Muốn phủ nốt nhóm này
  chỉ có hai đường: viết thêm một ibus engine, hoặc chạy app đó trên X11.

## 2. Kiến trúc

```
                    ┌──────────────────────────────────────┐
                    │  ukengine + vnconv + byteio (có sẵn) │
                    │  UkEngine::process → (backs, bytes)  │
                    └──────────────────┬───────────────────┘
                                       │ C API: libukint
                    ┌──────────────────▼───────────────────┐
                    │  ukbridge  (MỚI, thuần C, no-toolkit)│
                    │  biến (backs,bytes) → preedit/commit │
                    └──┬─────────┬─────────┬─────────┬─────┘
                       │         │         │         │
                 ┌─────▼───┐ ┌───▼────┐ ┌──▼────┐ ┌──▼──────────┐
                 │ GTK3    │ │ GTK4   │ │ Qt6   │ │ IBus engine │
                 │ module  │ │ module │ │ plugin│ │ (đường chính)│
                 └─────────┘ └────────┘ └───────┘ └─────────────┘
                       └─────────┴────────┴────────┴─► ~/.unikey/state
```

## 3. Thay đổi cốt lõi so với module GTK2 cũ: bỏ backspace giả, dùng preedit

Module GTK2 (đã gỡ bỏ ở bản này) commit thẳng từng ký tự, rồi khi cần sửa dấu thì **giả lập
phím Backspace** bằng `gdk_event_put()`. Cách này chỉ chạy trên X11, phá vỡ undo của app, và
hỏng với mọi text field ảo. Chính tác giả cũng ghi trong chú thích rằng
`delete-surrounding` của toolkit "unreliable" nên mới phải dùng phím giả — hai mươi năm sau,
cả hai vấn đề đó vẫn còn nguyên và định hình thiết kế hiện tại (xem README mục 6).

Bộ gõ hiện đại giữ âm tiết đang gõ trong **preedit** — vùng chữ gạch chân do app vẽ, chưa
vào văn bản thật:

```
gõ:  t i e e n g s      preedit: "t"→"ti"→"tie"→"tiê"→"tiên"→"tiếng"
gõ:  space              commit "tiếng ", preedit rỗng
```

`UkEngine` vốn trả về đúng cặp `(số backspace, bytes mới)`. Với preedit thì "backspace"
không còn là phím giả nữa — chỉ là xoá N ký tự cuối của chuỗi preedit ta tự giữ. Bộ máy
1.0 không phải sửa một dòng nào.

## 4. Lộ trình

| Giai đoạn | Nội dung | Phủ được | Trạng thái |
|---|---|---|---|
| 1 | `ukbridge` + module GTK3 `im-unikey.so` | gedit, LibreOffice, Firefox, app GTK3 | **xong** — đã xác nhận gõ được trong gedit chạy `GDK_BACKEND=wayland` |
| 2 | Module GTK4 `libim-unikey.so` | app GNOME đời mới (Text Editor, Files, Settings) | **xong** — module nạp được vào gnome-text-editor trên Wayland |
| 3 | Plugin Qt6 `libunikeyplatforminputcontextplugin.so` | app KDE/Qt | **xong** — plugin nạp được, `create("unikey")` trả về input context |
| 4 | Trạng thái dùng chung giữa các tiến trình | bật/tắt & kiểu gõ đồng bộ giữa mọi app | **xong** — qua `~/.unikey/state` + inotify |
| 5 | IBus engine | hiện trong Settings, phủ VSCode/Electron/GTK4 Wayland | **xong** — xem mục 6 |
| 6 | `zwp_input_method_v2` | sway, Hyprland (không dùng được trên GNOME) | chưa làm — GNOME không dùng được |

## 5. Trạng thái dùng chung: vì sao không cần daemon

fcitx5 dùng daemon + D-Bus để mọi app biết bộ gõ đang bật hay tắt. Ở đây mỗi ứng dụng nạp module
nên có engine riêng — vấn đề chỉ là **đồng bộ hai con số**: bật/tắt và kiểu gõ. Một daemon là quá
nặng cho việc đó.

Cách làm: `~/.unikey/state` (`enabled=` / `method=`), ghi kiểu nguyên tử bằng `rename()`, và mỗi
tiến trình theo dõi bằng **inotify** — lõi Linux, không thêm phụ thuộc nào. `ukbridge` chỉ lộ ra
`uk_bridge_state_fd()` và `uk_bridge_state_dispatch()`; front-end tự gắn fd vào vòng lặp sự kiện
của nó (`g_unix_fd_add` với GTK, `QSocketNotifier` với Qt).

Hai điểm phải để ý, cả hai đều đã có test:

- Theo dõi **thư mục** chứ không phải file. `rename()` thay inode nên watch đặt trên chính file sẽ
  mất hiệu lực ngay sau lần ghi đầu tiên.
- **Không lọc sự kiện do chính mình gây ra.** Bản đầu có cờ `writing_state` để bỏ qua, nhưng cờ đó
  kẹt lại từ lần ghi trước và nuốt mất thay đổi thật của tiến trình khác — test
  `app2 sang VNI -> app1 sang theo` bắt được đúng lỗi này. Đọc lại state của chính mình vốn đã là
  phép không làm gì, nên bỏ hẳn cờ là cách đúng.

## 6. IBus engine — vì sao vẫn cần, dù đã có module toolkit

Module GTK/Qt gõ được, nhưng người dùng phải tự đặt biến môi trường và đăng xuất/đăng nhập lại.
Trải nghiệm "vào Settings thêm bộ gõ là xong" thì **không thể** làm bằng module, và đây là lý do
kỹ thuật, không phải do chưa làm tới:

```
$ gsettings describe org.gnome.desktop.input-sources sources
"The first string is the type and can be one of "xkb" or "ibus"."

$ strings /usr/bin/gnome-control-center | grep 'CcInputSource'
CcInputSourceXkb
CcInputSourceIBus
```

Danh sách "Add Input Source" chỉ nhận đúng hai loại: layout **xkb** hoặc engine **ibus**. Không có
loại thứ ba. fcitx5 cũng không chen vào được — nên nó phải tự làm tray icon và app cấu hình riêng.

Vì vậy `src/unikey-ibus/` là front-end thứ tư, và là front-end duy nhất:

- hiện trong Settings → Keyboard → Input Sources → Vietnamese → "Vietnamese (UniKey)",
- chuyển bằng Super-Space, có chỉ báo trên thanh trên cùng, không cần biến môi trường nào,
- **gõ được vào VSCode và mọi app Electron/GTK4 Wayland** — vì Mutter nối `zwp_text_input_v3` vào
  ibus. Đây chính là nhóm app mà module toolkit (và cả fcitx5-unikey) không với tới trên GNOME.

Về "không phụ thuộc bên thứ ba": ibus không thật sự là bên thứ ba trên GNOME. `gnome-shell`
**Depends** `gir1.2-ibus-1.0` và `gnome-control-center` **Depends** `libibus-1.0-5` — nó là IM bus
mà GNOME đã dựng sẵn, khác với fcitx5 là framework phải cài thêm.

Bốn front-end dùng chung y hệt `ukbridge`; engine ibus không lặp lại một dòng logic gõ nào.

| Front-end | Hiện trong Settings | Cần biến môi trường | VSCode/Electron | Chạy được khi không có ibus |
|---|---|---|---|---|
| IBus engine | có | không | **có** | không |
| GTK3/GTK4/Qt6 | không | có | không | có |

## 7. Cài đặt

```sh
./autogen.sh && ./configure && make && make check
./make-deb.sh
sudo dpkg -i release/x-unikey_*.deb
```

Hoặc cài tay bằng `sudo ./install-standalone.sh` (vào `/usr/local`, tiện khi đang phát triển).
**Chỉ dùng một trong hai** — chi tiết ở README mục 3.

Cả hai đều chép 4 front-end vào thư mục hệ thống và cập nhật cache immodule của GTK3.

## 8. Hai chế độ commit — xem README mục 6

Engine tự chọn theo `IBUS_CAP_SURROUNDING_TEXT` mà ứng dụng khai báo: có thì **DIRECT**
(commit thẳng, không gạch chân), không thì **PREEDIT**. Đo thực tế: trình duyệt `caps=0x29`
→ DIRECT, gnome-terminal `caps=0x9` → PREEDIT.

Ba cách đã thử và thất bại (đừng làm lại) được ghi đầy đủ trong README mục 6 và CLAUDE.md.

Tầng XIM cũ (`ukxim`, `IMdkit`, cửa sổ `unikey`) và module GTK2 đã được gỡ bỏ: 16.160 dòng,
63% mã nguồn, chỉ phục vụ X11 mà không ai còn dùng trên desktop Wayland. Bộ máy gõ
(`ukengine`/`vnconv`/`byteio`) giữ nguyên không sửa một dòng.
