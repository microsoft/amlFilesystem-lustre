#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Run only in the exported source copy prepared by the external release tool.
set -euo pipefail
plan=$(realpath "${1:?prepared build directory required}")
source "$plan/native.env"
cd "$plan/source"
test ! -e "$plan/payload"
mkdir "$plan/payload"
case "$KIND" in
    dkms)
        dest="$plan/payload/usr/src/amlfs-lustre-client-$DKMS_VERSION"
        mkdir -p "$dest"
        cp -a . "$dest/"
        cp "$plan/dkms.conf" "$dest/dkms.conf"
        ;;
    utils|modules)
        source /etc/os-release
        case "$DISTRO:$ID:$VERSION_ID:$(uname -m)" in
            ubuntu*:"ubuntu":*:x86_64) native_arch=amd64 ;;
            ubuntu*:"ubuntu":*:aarch64) native_arch=arm64 ;;
            rhel*:"rhel":*:x86_64|azurelinux*:"azurelinux":*:x86_64) native_arch=x86_64 ;;
            rhel*:"rhel":*:aarch64|azurelinux*:"azurelinux":*:aarch64) native_arch=aarch64 ;;
            *) echo "Source build requires the matching native distro/architecture" >&2; exit 1 ;;
        esac
        if [[ "$DISTRO" = ubuntu* ]]; then native_distro="ubuntu$VERSION_ID"
        else native_distro="$ID${VERSION_ID%%.*}"; fi
        [ "$DISTRO" = "$native_distro" ] && [ "$ARCH" = "$native_arch" ] || {
            echo "Native source build target mismatch" >&2; exit 1
        }
        for tool in libtoolize aclocal autoheader automake autoconf make gcc; do
            command -v "$tool" >/dev/null || {
                echo "Missing source-build prerequisite: $tool" >&2
                exit 1
            }
        done
        args=(--disable-server --disable-ldiskfs --without-zfs --disable-tests
              --disable-iokit --disable-mpitests --disable-doc --disable-gss
              --disable-gss-keyring --disable-dependency-tracking --with-o2ib=no
              --with-systemdsystemunitdir=no --prefix=/usr --sbindir=/usr/sbin)
        if [ "$KIND" = modules ]; then
            test -n "$KERNEL_HEADERS"
            test -r "$KERNEL_HEADERS/include/generated/utsrelease.h"
            grep -Fx "#define UTS_RELEASE \"$KERNEL_RELEASE\"" \
                "$KERNEL_HEADERS/include/generated/utsrelease.h"
            args+=(--enable-modules --disable-utils --with-linux="$KERNEL_HEADERS"
                   --with-linux-obj="$KERNEL_HEADERS" --with-kmp-moddir=updates/amlfs-lustre)
        else
            args+=(--disable-modules --enable-utils)
        fi
        bash ./autogen.sh
        ./configure "${args[@]}"
        make -j "${BUILD_JOBS:-2}"
        make install DESTDIR="$plan/payload"
        if [ "$KIND" = modules ]; then
            # make install also stages configuration/headers. The module payload
            # owns only this exact target tree; utilities own shared host files.
            mkdir "$plan/module-tree"
            mv "$plan/payload/lib/modules/$KERNEL_RELEASE" "$plan/module-tree/"
            rm -r "$plan/payload"
            mkdir -p "$plan/payload/lib/modules"
            mv "$plan/module-tree/$KERNEL_RELEASE" "$plan/payload/lib/modules/"
            rmdir "$plan/module-tree"
            # Finalize unsigned ELF before the external signing step.
            find "$plan/payload" -type f -name '*.ko' -exec strip --strip-debug '{}' \;
        else
            # libtool archives and static libraries are not runtime payloads.
            find "$plan/payload" -type f \( -name '*.la' -o -name '*.a' \) -delete
        fi
        ;;
    prebuilt-selector|dkms-selector|tracker|guard) ;;
    *) echo "Unknown payload kind" >&2; exit 1 ;;
esac
printf '%s\n' "$SOURCE_COMMIT" > "$plan/payload-source-commit"
