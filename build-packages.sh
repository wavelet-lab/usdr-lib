#!/usr/bin/env bash

set -uo pipefail

current_target=""

handle_interrupt() {
    if [[ -n "$current_target" ]]; then
        printf '\nBuild interrupted: %s\n' "$current_target" >&2
    else
        printf '\nBuild interrupted.\n' >&2
    fi
    exit 130
}

trap handle_interrupt INT TERM

readonly DISTRIBUTIONS=(
    ubuntu-bionic
    ubuntu-focal
    ubuntu-jammy
    ubuntu-noble
    ubuntu-resolute
    debian-bookworm
)

readonly ARCHITECTURES=(
    amd64
    aarch64
)

show_error() {
    if command -v whiptail >/dev/null 2>&1 && [[ -t 1 ]]; then
        whiptail --title "Package builder" --msgbox "$1" 8 72
    else
        printf 'Error: %s\n' "$1" >&2
    fi
}

if ! command -v whiptail >/dev/null 2>&1; then
    printf '%s\n' "The 'whiptail' utility is required." \
        "Install it with: sudo apt install whiptail" >&2
    exit 1
fi

if ! command -v task >/dev/null 2>&1; then
    show_error "Go Task is not installed or is not available in PATH."
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    show_error "Docker is not installed or is not available in PATH."
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    show_error "Docker is not running or the current user cannot access it."
    exit 1
fi

mkdir -p output
if [[ ! -w output ]]; then
    show_error "The output directory is not writable. Fix its ownership with:\n\nsudo chown -R \"$USER:$USER\" output"
    exit 1
fi

distribution_choices=(
    ubuntu-bionic "Ubuntu 18.04 Bionic" off
    ubuntu-focal "Ubuntu 20.04 Focal" off
    ubuntu-jammy "Ubuntu 22.04 Jammy" off
    ubuntu-noble "Ubuntu 24.04 Noble" off
    ubuntu-resolute "Ubuntu 26.04 Resolute" off
    debian-bookworm "Debian 12 Bookworm" off
)

architecture_choices=(
    amd64 "AMD64 / x86-64" off
    aarch64 "ARM64 / AArch64" off
)

mode=$(whiptail \
    --title "uSDR package builder" \
    --menu "Choose build mode:" 12 64 2 \
    all "Build all distributions and architectures" \
    custom "Select distributions and architectures" \
    3>&1 1>&2 2>&3)
ui_status=$?
clear

if (( ui_status != 0 )); then
    echo "Build cancelled."
    exit 0
fi

if [[ "$mode" == "all" ]]; then
    selected_distributions=("${DISTRIBUTIONS[@]}")
    selected_architectures=("${ARCHITECTURES[@]}")
else
    distribution_selection=$(whiptail \
        --separate-output \
        --title "uSDR package builder" \
        --checklist "Select distributions:" 19 72 10 \
        "${distribution_choices[@]}" \
        3>&1 1>&2 2>&3)
    ui_status=$?
    clear

    if (( ui_status != 0 )); then
        echo "Build cancelled."
        exit 0
    fi

    architecture_selection=$(whiptail \
        --separate-output \
        --title "uSDR package builder" \
        --checklist "Select architectures:" 12 64 4 \
        "${architecture_choices[@]}" \
        3>&1 1>&2 2>&3)
    ui_status=$?
    clear

    if (( ui_status != 0 )); then
        echo "Build cancelled."
        exit 0
    fi

    if [[ -z "$distribution_selection" || -z "$architecture_selection" ]]; then
        echo "No distributions or architectures selected."
        exit 0
    fi

    mapfile -t selected_distributions <<< "$distribution_selection"
    mapfile -t selected_architectures <<< "$architecture_selection"
fi

selected=()
for distribution in "${selected_distributions[@]}"; do
    for architecture in "${selected_architectures[@]}"; do
        selected+=("${distribution}-${architecture}")
    done
done

successful=()
failed=()
log_dir="output/logs"
mkdir -p "$log_dir"

for target in "${selected[@]}"; do
    current_target="$target"
    log_file="${log_dir}/${target}.log"
    printf '\n===== Building %s =====\n\n' "$target"
    printf 'Log: %s\n\n' "$log_file"
    if task "box:${target}-deb" 2>&1 | tee "$log_file"; then
        successful+=("$target")
    else
        failed+=("$target")
    fi
done
current_target=""

summary="Build finished.\n\nSuccessful: ${#successful[@]}\nFailed: ${#failed[@]}"

if (( ${#failed[@]} > 0 )); then
    summary+="\n\nFailed targets:\n"
    printf -v failed_list '%s\n' "${failed[@]}"
    summary+="$failed_list"
    summary+="\nLogs: $log_dir/"
fi

whiptail --title "uSDR package builder" --msgbox "$summary" 18 72
clear
printf '%b\n' "$summary"

(( ${#failed[@]} == 0 ))
