#!/usr/bin/env bash

set -euo pipefail

readonly image=${1:?Docker image name is required}
readonly binfmt_image=tonistiigi/binfmt

test_dpkg_deb() {
    docker run --rm --platform linux/aarch64 "$image" sh -eu -c '
        workdir=$(mktemp -d)
        trap '\''rm -rf "$workdir"'\'' EXIT

        mkdir -p "$workdir/package/DEBIAN" "$workdir/package/usr/bin"
        printf "%s\n" \
            "Package: usdr-binfmt-check" \
            "Version: 1" \
            "Architecture: arm64" \
            "Maintainer: uSDR build system <noreply@localhost>" \
            "Description: ARM64 dpkg-deb compatibility check" \
            > "$workdir/package/DEBIAN/control"
        cp /bin/true "$workdir/package/usr/bin/usdr-binfmt-check"
        dpkg-deb --root-owner-group --build \
            "$workdir/package" "$workdir/check.deb" >/dev/null
    '
}

if test_dpkg_deb; then
    exit 0
fi

printf '%s\n' \
    'The current ARM64 binfmt/QEMU cannot run Ubuntu Resolute dpkg-deb.' \
    'Replacing only the ARM64 handler with an up-to-date implementation...'

docker run --privileged --rm "$binfmt_image" --uninstall arm64
docker run --privileged --rm "$binfmt_image" --install arm64

if ! test_dpkg_deb; then
    printf '%s\n' 'ARM64 dpkg-deb still fails after refreshing binfmt/QEMU.' >&2
    exit 1
fi

printf '%s\n' 'ARM64 binfmt/QEMU check passed after refreshing the handler.'
