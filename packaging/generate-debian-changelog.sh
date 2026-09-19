#!/usr/bin/env bash

set -euo pipefail

if (( $# != 4 )); then
    echo "Usage: $0 <source-changelog> <codename> <distribution> <output>" >&2
    exit 2
fi

source_changelog=$1
codename=$2
distribution=$3
output=$4
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# shellcheck disable=SC1091
source "$script_dir/package-metadata"

release_line=$(sed -n 's/^Release \([^ ]*\) (\([^)]*\))$/\1 \2/p' "$source_changelog" | head -n 1)
read -r version release_date <<< "$release_line"

if [[ -z "${version:-}" || -z "${release_date:-}" ]]; then
    echo "Cannot determine the latest release from $source_changelog" >&2
    exit 1
fi

mapfile -t changes < <(
    awk '
        /^Release / {
            if (release_seen) exit
            release_seen = 1
            next
        }
        release_seen && /^- / {
            sub(/^- /, "")
            print
        }
    ' "$source_changelog"
)

if (( ${#changes[@]} == 0 )); then
    changes=("Release $version")
fi

{
    printf 'usdr (%s~%s0) %s; urgency=low\n\n' "$version" "$codename" "$distribution"
    for change in "${changes[@]}"; do
        printf '  * %s\n' "$change"
    done
    printf '\n -- %s <%s>  %s\n' "$PACKAGE_MAINTAINER_NAME" "$PACKAGE_MAINTAINER_EMAIL" \
        "$(date --date="$release_date 00:00:00 UTC" --rfc-email --utc)"
} > "$output"
