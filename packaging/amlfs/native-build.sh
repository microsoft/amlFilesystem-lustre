#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Consumes rendered native metadata; no Python, signing, upload or host install.
set -euo pipefail
plan=$(realpath "${1:?prepared build directory required}")
source "$plan/native.env"
test -d "$plan/payload"
test -s "$plan/payload-inventory.json"
if [ "$KIND" = utils ] && [ "$FIXTURE_PAYLOAD" = 0 ]; then
    # Shared-library dependency discovery must use the target's native database,
    # even when preparation/sealing/verification ran outside this container.
    bash "$plan/source/packaging/amlfs/check-platform.sh" "$plan"
fi
test ! -e "$plan/root"
mkdir "$plan/root"
cp -a "$plan/payload/." "$plan/root/"
mkdir -p "$plan/root/usr/share/amlfs-lustre/packages/$NAME"
cp "$plan/provenance.json" "$plan/root/usr/share/amlfs-lustre/packages/$NAME/"
mkdir "$plan/artifacts"
if [ "$BACKEND" = deb ]; then
    mkdir -p "$plan/root/DEBIAN" "$plan/debian"
    cp "$plan/control" "$plan/root/DEBIAN/control"
    if [ "$KIND" = utils ]; then
        cd "$plan"
        printf 'Source: %s\n\nPackage: %s\nArchitecture: %s\n' \
            "$NAME" "$NAME" "$ARCH" > debian/control
        # Produce symbols for bundled libraries before resolving binaries.
        dpkg-gensymbols -p"$NAME" -P"$plan/root" -v"$VERSION"
        elf=()
        while IFS= read -r -d '' file; do
            if [ "$(head -c 4 "$file")" = $'\177ELF' ]; then
                elf+=("-e$file")
            fi
        done < <(find "$plan/root" -type f -print0)
        test "${#elf[@]}" -gt 0
        libs=()
        while IFS= read -r -d '' dir; do libs+=("-l$dir"); done \
            < <(find "$plan/root/usr" -type d -name '*lib*' -print0)
        deps=$(dpkg-shlibdeps -O -S"$plan/root" -x"$NAME" "${libs[@]}" "${elf[@]}")
        deps=${deps#shlibs:Depends=}
        test -n "$deps"
        # The template already has explicit non-ELF runtime dependencies.
        sed -i "/^Depends: /s/$/, $deps/" "$plan/root/DEBIAN/control"
    fi
    for script in preinst postinst prerm postrm; do
        if [ -f "$plan/$script" ]; then
            install -m 0755 "$plan/$script" "$plan/root/DEBIAN/$script"
        fi
    done
    if [ -d "$plan/root/etc" ]; then
        (cd "$plan/root" && find etc -type f | sed 's,^,/,') > "$plan/root/DEBIAN/conffiles"
    fi
    dpkg-deb --root-owner-group --build "$plan/root" \
        "$plan/artifacts/${NAME}_${VERSION}_${ARCH}.deb"
else
    mkdir -p "$plan/rpm"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS,work}
    (
        cd "$plan/root"
        while IFS= read -r -d '' file; do
            case "$file" in
                *\"*|*%*|*$'\n'*) echo "Unsafe RPM payload path" >&2; exit 1 ;;
            esac
            if [ -d "$file" ] && [ ! -L "$file" ]; then
                # FHS ancestors belong to the distro, including its 0555 modes
                # and merged-/usr symlinks, not to this staged payload.
                case "$file" in
                    bin|sbin|lib|lib64|etc|usr|var|run|opt|\
                    usr/bin|usr/sbin|usr/lib|usr/lib64|usr/include|usr/src|usr/share|\
                    usr/share/doc|usr/share/man|usr/share/man/man[1-9]|\
                    lib/modules|usr/lib/modules|var/lib|var/run) continue ;;
                esac
                printf '%%dir "/%s"\n' "$file"
            elif [[ "$file" = etc/* ]]; then
                printf '%%config(noreplace) "/%s"\n' "$file"
            else
                printf '"/%s"\n' "$file"
            fi
        done < <(find . -mindepth 1 -printf '%P\0')
    ) > "$plan/files.list"
    # Nothing in the signed tree is stripped, relinked or compressed in place.
    rpmbuild -bb "$plan/package.spec" \
        --define "_topdir $plan/rpm" --define "_tmppath $plan/rpm/work" \
        --define "_buildrootdir $plan/rpm/BUILDROOT" \
        --define "_build_id_links none"
    find "$plan/rpm/RPMS" -type f -name '*.rpm' -exec cp '{}' "$plan/artifacts/" \;
fi
