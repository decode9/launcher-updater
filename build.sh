#!/usr/bin/env bash
# Builds build/launcher.exe (Windows i686, GUI subsystem) with clang + lld-link, without a C
# runtime or Windows SDK: import libraries come from src/platform/win32/*.def via llvm-dlltool.
# Needs: clang, lld (lld-link), llvm (llvm-dlltool). Usage: ./build.sh
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
out="$root/build"
obj="$out/obj"
mkdir -p "$obj"
version="$(tr -d ' \r\n' < "$root/VERSION")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "VERSION must be X.Y.Z" >&2; exit 1; }

libs=()
for def in "$root"/src/platform/win32/*.def; do
    name="$(basename "$def" .def)"
    llvm-dlltool -m i386 -k -d "$def" -l "$obj/$name.lib"
    libs+=("$obj/$name.lib")
done

# -fno-builtin keeps clang from turning loops into calls the missing CRT would have to provide;
# rt.c supplies the memset/memcpy family it still emits. Stack probes stay enabled on purpose:
# there is no __chkstk, so a >4 KiB frame fails the link instead of skipping the guard page.
cflags=(--target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector
        -std=c11 -Wall -Wextra -Werror -I"$root/src" "-DLAUNCHER_VERSION=\"$version\"")
objs=()
for src in "$root"/src/core/*.c "$root"/src/platform/win32/*.c "$root"/src/platform/win32/ui/*.c "$root"/src/app/*.c "$root"/src/main.c; do
    rel="${src#"$root"/src/}"
    o="$obj/${rel//\//_}"
    o="${o%.c}.obj"
    clang "${cflags[@]}" -c "$src" -o "$o"
    objs+=("$o")
done

# Embedded neutral theme (the look used before the server's theme arrives).
python3 "$root/tools/embed.py" "$root/examples/theme-neutral/theme.json" "$out/gen/theme_neutral.c" theme_neutral
clang "${cflags[@]}" -c "$out/gen/theme_neutral.c" -o "$obj/gen_theme_neutral.obj"
objs+=("$obj/gen_theme_neutral.obj")

# Resources: generated icon, version information (from VERSION) and application manifest.
mkdir -p "$out/gen"
python3 "$root/tools/make-icon.py" "$out/gen/launcher.ico"
IFS=. read -r v_major v_minor v_patch <<< "$version"
llvm-rc /nologo /I "$root/src/platform/win32/res" /D "VER_MAJOR=$v_major" /D "VER_MINOR=$v_minor" \
        /D "VER_PATCH=$v_patch" /D "VER_STRING=\"$version\"" /D "ICON_PATH=\"$out/gen/launcher.ico\"" \
        /FO "$obj/launcher.res" "$root/src/platform/win32/res/launcher.rc"
objs+=("$obj/launcher.res")

lld-link /nologo /machine:x86 /subsystem:windows,5.01 /nodefaultlib /entry:entry \
         /dynamicbase /nxcompat /safeseh:no /opt:ref /out:"$out/launcher.exe" "${objs[@]}" "${libs[@]}"
echo "OK: $out/launcher.exe ($(stat -c %s "$out/launcher.exe") bytes, version $version)"
