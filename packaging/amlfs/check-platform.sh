#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -euo pipefail
plan=$(realpath "${1:?prepared build directory required}")
source "$plan/native.env"
source /etc/os-release
case "$DISTRO:$ID:$VERSION_ID:$(uname -m)" in
    ubuntu*:"ubuntu":*:x86_64) native_arch=amd64 ;;
    ubuntu*:"ubuntu":*:aarch64) native_arch=arm64 ;;
    rhel*:"rhel":*:x86_64|azurelinux*:"azurelinux":*:x86_64) native_arch=x86_64 ;;
    rhel*:"rhel":*:aarch64|azurelinux*:"azurelinux":*:aarch64) native_arch=aarch64 ;;
    *) echo "Build requires the matching native distro/architecture" >&2; exit 1 ;;
esac
if [[ "$DISTRO" = ubuntu* ]]; then native_distro="ubuntu$VERSION_ID"
else native_distro="$ID${VERSION_ID%%.*}"; fi
[ "$DISTRO" = "$native_distro" ] && [ "$ARCH" = "$native_arch" ] || {
    echo "Native build target mismatch" >&2; exit 1
}
