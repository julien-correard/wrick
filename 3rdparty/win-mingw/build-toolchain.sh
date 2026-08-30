#!/usr/bin/env bash
#
# 3rdparty/win-mingw/build-toolchain.sh
#
# Rebuilds the Windows cross-compilation toolchain for xrick:
#   - zlib 1.3.1          -> zlib-1.3.1/  (libz.a + headers)
#   - SDL 1.2.15          -> SDL-1.2.15/  (headers + libSDL.a + SDL.dll)
#
# The installed result lives next to this script inside the repo, so that
# Makefile.win can build xrick.exe without depending on a volatile /tmp.
#
# Requires: gcc-mingw-w64-i686 (i686-w64-mingw32-gcc)
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ZLIB_VER=1.3.1
SDL_VER=1.2.15

CC=i686-w64-mingw32-gcc
AR="i686-w64-mingw32-ar"
RANLIB="i686-w64-mingw32-ranlib"

command -v "$CC" >/dev/null 2>&1 || {
    echo "Erreur: $CC introuvable. Installe le paquet gcc-mingw-w64-i686." >&2
    exit 1
}

echo "== zlib $ZLIB_VER =="
cd "$WORK"
curl -sL -o zlib.tar.gz "https://github.com/madler/zlib/archive/refs/tags/v$ZLIB_VER.tar.gz"
tar xzf zlib.tar.gz
cd "zlib-$ZLIB_VER"
CC="$CC" AR="$AR" RANLIB="$RANLIB" ./configure --prefix="$HERE/zlib-$ZLIB_VER" >/dev/null
make -j"$(nproc)" libz.a >/dev/null
mkdir -p "$HERE/zlib-$ZLIB_VER"
cp zlib.h zconf.h "$HERE/zlib-$ZLIB_VER/"
cp libz.a "$HERE/zlib-$ZLIB_VER/"

echo "== SDL $SDL_VER =="
cd "$WORK"
curl -sL -o SDL.tar.gz "https://www.libsdl.org/release/SDL-$SDL_VER.tar.gz"
tar xzf SDL.tar.gz
cd "SDL-$SDL_VER"
./configure --host=i686-w64-mingw32 --prefix="$HERE/SDL-$SDL_VER" >/dev/null
make -j"$(nproc)" >/dev/null
make install >/dev/null
# Relink SDL.dll without the libgcc_s DLL dependency (self-contained).
"$CC" -shared -static-libgcc \
    build/.libs/*.o \
    -luser32 -lgdi32 -lwinmm -ldxguid \
    -o "$HERE/SDL-$SDL_VER/bin/SDL.dll" \
    -Wl,--enable-auto-image-base \
    -Xlinker --out-implib -Xlinker "$HERE/SDL-$SDL_VER/lib/libSDL.dll.a"

# Trim the installed tree to the minimum needed by Makefile.win.
rm -rf "$HERE/SDL-$SDL_VER"/{build*,docs,man,share,src,test,VisualC,VisualCE,Xcode,\
    acinclude,autogen.sh,build-deps,build-scripts,configure*,*document*,CWprojects*,MPWmake*,Watcom*,Borland*}
rm -f "$HERE/SDL-$SDL_VER"/{Makefile*,sdl-config,libtool,sdl.m4,sdl.pc*,config.*,\
    SDL.qpg*,*.html,*.zip,*.bin,*.spec,README*,TODO,COPYING,CREDITS,WhatsNew,INSTALL,BUGS}

echo "== Terminé =="
echo "Résultat:"
du -sh "$HERE/zlib-$ZLIB_VER" "$HERE/SDL-$SDL_VER"
