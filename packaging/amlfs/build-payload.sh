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
        bash packaging/amlfs/check-platform.sh "$plan"
        for tool in libtoolize aclocal autoheader automake autoconf make gcc; do
            command -v "$tool" >/dev/null || {
                echo "Missing source-build prerequisite: $tool" >&2
                exit 1
            }
        done
        args=(--disable-server --disable-ldiskfs --without-zfs --disable-tests
              --disable-iokit --disable-mpitests --disable-doc --disable-gss
              --disable-gss-keyring --disable-dependency-tracking --with-o2ib=yes
              --enable-client --enable-quota
              --with-systemdsystemunitdir=no --prefix=/usr --sbindir=/usr/sbin)
        # The supported source build also needs kernel checks for utilities.
        # Match the legacy in-kernel RDMA profile, not mutable external OFED.
        if command -v ofed_info >/dev/null; then
            echo "External OFED requires a separately qualified build profile" >&2
            exit 1
        fi
        test -n "$KERNEL_HEADERS"
        test -r "$KERNEL_HEADERS/include/generated/utsrelease.h"
        grep -Fx "#define UTS_RELEASE \"$KERNEL_RELEASE\"" \
            "$KERNEL_HEADERS/include/generated/utsrelease.h"
        args+=(--enable-modules --with-linux="$KERNEL_HEADERS"
               --with-linux-obj="$KERNEL_HEADERS" --with-kmp-moddir=updates/amlfs-lustre)
        if [[ "$DISTRO" = ubuntu* ]]; then args+=(--enable-crypto); fi
        if [ "$KIND" = modules ]; then
            args+=(--disable-utils)
        else
            args+=(--enable-utils)
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
            # Kernel checks/builds do not make modules part of the utility payload.
            rm -r "$plan/payload/lib/modules"
            # libtool archives and static libraries are not runtime payloads.
            find "$plan/payload" -type f \( -name '*.la' -o -name '*.a' \) -delete
        fi
        ;;
    prebuilt-selector|dkms-selector|tracker|guard) ;;
    *) echo "Unknown payload kind" >&2; exit 1 ;;
esac
printf '%s\n' "$SOURCE_COMMIT" > "$plan/payload-source-commit"
