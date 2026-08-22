#!/bin/sh
# Dong goi x-unikey thanh .deb, ket qua nam trong release/
#
#   ./configure && make          # phai build truoc
#   ./make-deb.sh
#
# Khong can root: dpkg-deb --root-owner-group dat quyen root cho file trong
# goi ma khong can chay bang root.

set -e
umask 022

VERSION="${VERSION:-1.0.4}"
REVISION="${REVISION:-14}"
ARCH="$(dpkg --print-architecture)"
PKG="x-unikey"
OUT="release"
STAGE="$OUT/${PKG}_${VERSION}-${REVISION}_${ARCH}"

MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
GTK3_BINVER="$(pkg-config gtk+-3.0 --variable=gtk_binary_version 2>/dev/null || echo 3.0.0)"
GTK4_BINVER="$(pkg-config gtk4 --variable=gtk_binary_version 2>/dev/null || echo 4.0.0)"

# Trong goi thi dung /usr, khong phai /usr/local: /usr/local danh cho thu
# nguoi dung tu cai bang tay, con dpkg quan ly /usr.
ENGINE_DIR="/usr/libexec/ibus-unikey"
COMPONENT_DIR="/usr/share/ibus/component"
APPLICATIONS_DIR="/usr/share/applications"
PROFILE_DIR="/usr/share/x-unikey"
GTK3_DIR="/usr/lib/$MULTIARCH/gtk-3.0/$GTK3_BINVER/immodules"
GTK4_DIR="/usr/lib/$MULTIARCH/gtk-4.0/$GTK4_BINVER/immodules"
QT6_DIR="/usr/lib/$MULTIARCH/qt6/plugins/platforminputcontexts"

say() { printf '%s\n' "$*"; }

#---------------------------------------------------------------
# Kiem tra da build chua
#---------------------------------------------------------------
ENGINE_BIN="src/unikey-ibus/ibus-engine-unikey"
SETUP_BIN="src/unikey-ibus/ibus-setup-unikey"
GTK3_SO="src/unikey-gtk3/.libs/im-unikey.so"
GTK4_SO="src/unikey-gtk4/.libs/libim-unikey.so"
QT6_SO="src/unikey-qt/.libs/libunikeyplatforminputcontextplugin.so"

for f in "$ENGINE_BIN" "$SETUP_BIN" "$GTK3_SO" "$GTK4_SO" "$QT6_SO"; do
    if [ ! -f "$f" ]; then
        say "Chua build: thieu $f"
        say "Chay truoc:  ./configure && make"
        exit 1
    fi
done

rm -rf "$STAGE"
mkdir -p "$OUT"

#---------------------------------------------------------------
# Chep file vao cay tam
#---------------------------------------------------------------
say "Dung cay thu muc goi..."

install -Dm755 "$ENGINE_BIN" "$STAGE$ENGINE_DIR/ibus-engine-unikey"
install -Dm755 "$SETUP_BIN"  "$STAGE$ENGINE_DIR/ibus-setup-unikey"
install -Dm755 "$GTK3_SO"    "$STAGE$GTK3_DIR/im-unikey.so"
install -Dm755 "$GTK4_SO"    "$STAGE$GTK4_DIR/libim-unikey.so"
install -Dm755 "$QT6_SO"     "$STAGE$QT6_DIR/libunikeyplatforminputcontextplugin.so"
install -Dm755 integration/unikey-profile "$STAGE/usr/bin/unikey-profile"
install -Dm644 integration/profile-zsh.zsh \
    "$STAGE$PROFILE_DIR/profile-zsh.zsh"

# Component XML: duong dan <exec> phai tro dung cho engine trong goi
mkdir -p "$STAGE$COMPONENT_DIR"
sed -e "s|@LIBEXECDIR@|$ENGINE_DIR|g" -e "s|@VERSION@|$VERSION|g" \
    -e "s|@SETUP_ELEMENT@|<setup>$ENGINE_DIR/ibus-setup-unikey</setup>|g" \
    src/unikey-ibus/unikey.xml.in > "$STAGE$COMPONENT_DIR/unikey.xml"
chmod 644 "$STAGE$COMPONENT_DIR/unikey.xml"

# Launcher an de GNOME Settings mo Preferences cua input source. Day khong
# phai autostart va khong tao cua so/tray icon thuong truc.
mkdir -p "$STAGE$APPLICATIONS_DIR"
sed -e "s|@LIBEXECDIR@|$ENGINE_DIR|g" \
    src/unikey-ibus/ibus-setup-unikey.desktop.in \
    > "$STAGE$APPLICATIONS_DIR/ibus-setup-unikey.desktop"
chmod 644 "$STAGE$APPLICATIONS_DIR/ibus-setup-unikey.desktop"

# Tai lieu
install -Dm644 README            "$STAGE/usr/share/doc/$PKG/README"
install -Dm644 doc/STANDALONE.md "$STAGE/usr/share/doc/$PKG/STANDALONE.md"
install -Dm644 COPYING           "$STAGE/usr/share/doc/$PKG/copyright"
install -Dm644 doc/ukmacro       "$STAGE/usr/share/doc/$PKG/examples/ukmacro"
for f in doc/im-samples/*; do
    [ -f "$f" ] && install -Dm644 "$f" "$STAGE/usr/share/doc/$PKG/examples/im-samples/$(basename "$f")"
done

#---------------------------------------------------------------
# Phu thuoc: hoi dpkg-shlibdeps thay vi doan
#---------------------------------------------------------------
say "Tinh phu thuoc thu vien..."
mkdir -p "$STAGE/DEBIAN"

DEPS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    # dpkg-shlibdeps doi co debian/control ben canh. Tao tam roi xoa, khong
    # de lai rac trong cay nguon.
    CLEANUP_DEBIAN=""
    if [ ! -d debian ]; then
        CLEANUP_DEBIAN=yes
        mkdir -p debian
        printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$PKG" "$PKG" > debian/control
    fi
    if dpkg-shlibdeps -O --ignore-missing-info \
        "$STAGE$ENGINE_DIR/ibus-engine-unikey" \
        "$STAGE$ENGINE_DIR/ibus-setup-unikey" \
        "$STAGE$GTK3_DIR/im-unikey.so" \
        "$STAGE$GTK4_DIR/libim-unikey.so" \
        "$STAGE$QT6_DIR/libunikeyplatforminputcontextplugin.so" \
        > "$OUT/.shlibdeps" 2>/dev/null
    then
        DEPS="$(sed -n 's/^shlibs:Depends=//p' "$OUT/.shlibdeps")"
    fi
    rm -f "$OUT/.shlibdeps"
    [ -n "$CLEANUP_DEBIAN" ] && rm -rf debian
fi

if [ -z "$DEPS" ]; then
    say "  (dpkg-shlibdeps khong chay duoc, dung danh sach toi thieu)"
    DEPS="libc6, libglib2.0-0t64 | libglib2.0-0, libibus-1.0-5, libgtk-3-0t64 | libgtk-3-0, libgtk-4-1, libqt6gui6, libx11-6, libstdc++6"
fi

INSTALLED_SIZE="$(du -sk --apparent-size "$STAGE" | cut -f1)"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION-$REVISION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: $DEPS
Recommends: ibus
Installed-Size: $INSTALLED_SIZE
Maintainer: UniKey team <unikey@gmail.com>
Homepage: https://unikey.org
Description: Bo go tieng Viet UniKey cho X11 va Wayland
 UniKey Vietnamese input method. Goi nay gom bon front-end dung chung mot
 bo may go:
 .
  * IBus engine  - hien trong Settings -> Keyboard -> Input Sources,
                   go duoc trong ca VSCode va cac ung dung Electron/GTK4
                   chay tren Wayland
  * Cua so setup - Telex/VNI/VIQR, terminal mode, dat dau va spell-check
  * Profile zsh  - tat/preedit dung rieng terminal, khong anh huong VSCode chat
  * Module GTK3 va GTK4
  * Plugin Qt6
 .
 Ho tro cac kieu go Telex, VNI, VIQR va kieu go tu dinh nghia.
EOF

#---------------------------------------------------------------
# postinst / postrm: cap nhat cache module cua GTK3
#---------------------------------------------------------------
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

update_gtk3_cache() {
    for q in /usr/lib/*/libgtk-3-0/gtk-query-immodules-3.0 \
             /usr/bin/gtk-query-immodules-3.0 ; do
        [ -x "$q" ] || continue
        "$q" --update-cache 2>/dev/null || true
        return
    done
}

case "$1" in
    configure)
        update_gtk3_cache
        echo ""
        echo "x-unikey da cai xong."
        echo ""
        echo "  1. Dang xuat roi dang nhap lai  (BAT BUOC neu vua cai ibus lan dau:"
        echo "     gnome-shell chi khoi dong ibus mot lan luc phien bat dau)"
        echo "  2. Settings -> Keyboard -> Input Sources -> +  -> Vietnamese"
        echo "     -> \"Vietnamese (UniKey)\""
        echo "  3. Chuyen bo go bang Super-Space"
        echo "  4. Menu VN -> Cai dat... de chon kieu go va cac tuy chon"
        echo "  5. Neu dung zsh trong VS Code terminal, them vao ~/.zshrc:"
        echo "     source /usr/share/x-unikey/profile-zsh.zsh"
        echo ""
        ;;
esac

exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e

case "$1" in
    remove|purge)
        for q in /usr/lib/*/libgtk-3-0/gtk-query-immodules-3.0 \
                 /usr/bin/gtk-query-immodules-3.0 ; do
            [ -x "$q" ] || continue
            "$q" --update-cache 2>/dev/null || true
            break
        done
        ;;
esac

exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postrm"

#---------------------------------------------------------------
# Dong goi
#---------------------------------------------------------------
say "Dong goi..."
dpkg-deb --root-owner-group --build "$STAGE" > /dev/null

DEB="$OUT/${PKG}_${VERSION}-${REVISION}_${ARCH}.deb"
rm -rf "$STAGE"

say ""
say "Xong: $DEB"
say ""
dpkg-deb --info "$DEB" | sed -n '1,12p'
say "Noi dung:"
dpkg-deb --contents "$DEB" | awk '{print "  " $6}' | grep -vE '/$' | head -20
