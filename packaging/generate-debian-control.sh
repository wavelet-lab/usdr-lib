#!/usr/bin/env bash

set -euo pipefail

if (( $# != 2 )); then
    echo "Usage: $0 <package-profile> <output>" >&2
    exit 2
fi

profile=$1
output=$2
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# shellcheck disable=SC1091
source "$script_dir/package-metadata"

case "$profile" in
    ubuntu-bionic)
        build_depends=(
            debhelper
            dkms
            build-essential
            dwarves
            libusb-1.0-0-dev
            libsoapysdr-dev
            check
        )
        ;;
    ubuntu-focal|ubuntu-jammy)
        build_depends=(
            "debhelper (>= 12.10)"
            dkms
            build-essential
            cmake
            python3
            python3-yaml
            dwarves
            libusb-1.0-0-dev
            libsoapysdr-dev
            check
        )
        ;;
    ubuntu-noble|ubuntu-resolute)
        build_depends=(
            "debhelper (>= 12.10)"
            dkms
            dh-dkms
            build-essential
            cmake
            python3
            python3-yaml
            dwarves
            libusb-1.0-0-dev
            libsoapysdr-dev
            check
        )
        ;;
    debian-bookworm)
        build_depends=(
            "debhelper (>= 12.10)"
            "dkms | dh-dkms"
            build-essential
            cmake
            python3
            python3-yaml
            dwarves
            libusb-1.0-0-dev
            libsoapysdr-dev
            check
        )
        ;;
    *)
        echo "Unsupported package profile: $profile" >&2
        exit 1
        ;;
esac

{
    printf 'Source: usdr\n'
    printf 'Section: misc\n'
    printf 'Priority: optional\n'
    printf 'Maintainer: %s <%s>\n' "$PACKAGE_MAINTAINER_NAME" "$PACKAGE_MAINTAINER_EMAIL"
    printf 'Standards-Version: %s\n' "$DEBIAN_STANDARDS_VERSION"
    printf 'Homepage: https://github.com/wavelet-lab/usdr-lib\n'
    printf 'Vcs-Browser: https://github.com/wavelet-lab/usdr-lib\n'
    printf 'Vcs-Git: https://github.com/wavelet-lab/usdr-lib.git\n'
    printf 'Build-Depends:\n'

    last_index=$(( ${#build_depends[@]} - 1 ))
    for index in "${!build_depends[@]}"; do
        separator=,
        if (( index == last_index )); then
            separator=
        fi
        printf '     %s%s\n' "${build_depends[index]}" "$separator"
    done

    printf '\n'
    cat "$script_dir/control-packages"
} > "$output"
