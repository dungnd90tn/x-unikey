#!/bin/sh
# Cai dat x-unikey ban standalone (khong can ibus/fcitx5).
#
#   ./install-standalone.sh          cai
#   ./install-standalone.sh remove   go
#
# Phai chay bang sudo cho phan chep module vao thu muc he thong.
# Phan cau hinh phien lam viec (~/.config) thi chay bang tai khoan thuong.

set -e

ACTION="${1:-install}"

# Chay duoi sudo thi $HOME co the la /root -- lay home that cua nguoi dung.
REAL_USER="${SUDO_USER:-$(id -un)}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

GTK3_DIR="$(pkg-config gtk+-3.0 --variable=libdir 2>/dev/null)/gtk-3.0/$(pkg-config gtk+-3.0 --variable=gtk_binary_version 2>/dev/null)/immodules"
GTK4_DIR="$(pkg-config gtk4 --variable=libdir 2>/dev/null)/gtk-4.0/$(pkg-config gtk4 --variable=gtk_binary_version 2>/dev/null)/immodules"
QT6_DIR="$(pkg-config Qt6Gui --variable=libdir 2>/dev/null)/qt6/plugins/platforminputcontexts"

GTK3_SO="src/unikey-gtk3/.libs/im-unikey.so"
GTK4_SO="src/unikey-gtk4/.libs/libim-unikey.so"
QT6_SO="src/unikey-qt/.libs/libunikeyplatforminputcontextplugin.so"

IBUS_BIN="src/unikey-ibus/ibus-engine-unikey"
IBUS_XML="src/unikey-ibus/unikey.xml"
IBUS_LIBEXEC="/usr/local/libexec"
IBUS_COMPONENT="/usr/share/ibus/component"

ENVFILE="$REAL_HOME/.config/environment.d/unikey.conf"

say() { printf '%s\n' "$*"; }

need_root() {
    if [ "$(id -u)" != "0" ]; then
        say "Can quyen root de chep module vao thu muc he thong."
        say "Chay lai: sudo $0 $ACTION"
        exit 1
    fi
}

refresh_gtk3_cache() {
    # GTK3 doc danh sach module tu mot file cache; khong cap nhat thi module
    # moi chep vao se khong duoc nhin thay.
    for q in /usr/lib/*/libgtk-3-0/gtk-query-immodules-3.0 \
             /usr/bin/gtk-query-immodules-3.0 ; do
        [ -x "$q" ] || continue
        for cache in /usr/lib/*/gtk-3.0/3.0.0/immodules.cache ; do
            [ -e "$cache" ] || continue
            say "  cap nhat $cache"
            if "$q" --update-cache 2>/dev/null ; then
                return
            fi
            # Ghi ra file tam roi moi thay the. Neu ghi thang bang '>' thi
            # cache bi cat rong TRUOC khi lenh chay; lenh that bai la GTK3 mat
            # toan bo input module cua he thong, khong chi rieng unikey.
            if "$q" > "$cache.tmp" 2>/dev/null && [ -s "$cache.tmp" ] ; then
                mv "$cache.tmp" "$cache"
            else
                rm -f "$cache.tmp"
                say "  (khong cap nhat duoc cache, giu nguyen cache cu)"
            fi
            return
        done
    done
    say "  (khong tim thay gtk-query-immodules-3.0, bo qua cache GTK3)"
}

do_install() {
    need_root

    for f in "$GTK3_SO" "$GTK4_SO" "$QT6_SO"; do
        if [ ! -f "$f" ]; then
            say "Chua build: thieu $f"
            say "Chay: ./configure && make"
            exit 1
        fi
    done

    say "Cai module:"
    install -Dm755 "$GTK3_SO" "$GTK3_DIR/im-unikey.so"       && say "  GTK3 -> $GTK3_DIR/im-unikey.so"
    install -Dm755 "$GTK4_SO" "$GTK4_DIR/libim-unikey.so"    && say "  GTK4 -> $GTK4_DIR/libim-unikey.so"
    install -Dm755 "$QT6_SO"  "$QT6_DIR/libunikeyplatforminputcontextplugin.so" \
                                                             && say "  Qt6  -> $QT6_DIR/"
    refresh_gtk3_cache

    # IBus engine: day moi la thu hien trong Settings -> Input Sources.
    if [ -f "$IBUS_BIN" ] && [ -f "$IBUS_XML" ]; then
        say ""
        say "Cai IBus engine:"
        install -Dm755 "$IBUS_BIN" "$IBUS_LIBEXEC/ibus-engine-unikey"
        say "  $IBUS_LIBEXEC/ibus-engine-unikey"
        install -Dm644 "$IBUS_XML" "$IBUS_COMPONENT/unikey.xml"
        say "  $IBUS_COMPONENT/unikey.xml"
        HAVE_IBUS=yes
    else
        say ""
        say "(Bo qua IBus engine: chua build. Cai libibus-1.0-dev roi ./configure && make)"
        HAVE_IBUS=no
    fi

    say ""
    say "Cau hinh phien lam viec cho nguoi dung $REAL_USER:"
    mkdir -p "$(dirname "$ENVFILE")"
    cat > "$ENVFILE" <<'EOF'
# x-unikey standalone -- khong dung ibus/fcitx5.
GTK_IM_MODULE=unikey
QT_IM_MODULE=unikey
XMODIFIERS=@im=unikey
EOF
    chown "$REAL_USER" "$ENVFILE" 2>/dev/null || true
    say "  $ENVFILE"

    say ""
    if [ "$HAVE_IBUS" = "yes" ]; then
        say "=== Cach dung khuyen nghi: qua Settings (giong moi bo go khac) ==="
        say ""
        say "  1. Khoi dong lai ibus:   ibus restart"
        say "  2. Settings -> Keyboard -> Input Sources -> +  -> Vietnamese"
        say "     -> chon \"Vietnamese (UniKey)\""
        say "  3. Chuyen bo go bang Super-Space nhu binh thuong."
        say ""
        say "Cach nay phu duoc ca VSCode va cac app Electron/GTK4 Wayland."
        say ""
        say "--- Cach thay the: module toolkit (khong can ibus) ---"
    fi
    say "Dat GTK_IM_MODULE/QT_IM_MODULE=unikey (da ghi san o tren), dang xuat roi"
    say "dang nhap lai. Thu ngay khong can dang xuat:"
    say "  GTK_IM_MODULE=unikey gedit                 # GTK3"
    say "  GTK_IM_MODULE=unikey gnome-text-editor     # GTK4"
    say ""
    say "Ctrl-Shift bat/tat, Ctrl-Shift-F5..F8 doi kieu go."
    say "Cau hinh: ~/.unikey/options   Trang thai dung chung: ~/.unikey/state"
}

do_remove() {
    need_root
    say "Go module:"
    for f in "$GTK3_DIR/im-unikey.so" \
             "$GTK4_DIR/libim-unikey.so" \
             "$QT6_DIR/libunikeyplatforminputcontextplugin.so" \
             "$IBUS_LIBEXEC/ibus-engine-unikey" \
             "$IBUS_COMPONENT/unikey.xml" ; do
        if [ -f "$f" ]; then
            rm -f "$f" && say "  xoa $f"
        fi
    done
    refresh_gtk3_cache
    if [ -f "$ENVFILE" ]; then
        rm -f "$ENVFILE" && say "  xoa $ENVFILE"
    fi
    say "Xong. Dang xuat roi dang nhap lai."
}

case "$ACTION" in
    install) do_install ;;
    remove|uninstall) do_remove ;;
    *) say "Dung: $0 [install|remove]" ; exit 1 ;;
esac
