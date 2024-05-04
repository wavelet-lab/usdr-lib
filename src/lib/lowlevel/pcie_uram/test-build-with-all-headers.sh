#!/usr/bin/env bash

set -e

# Define module directory
module_dir="driver"

# For each installed header try to build driver
for i in /lib/modules/*; do
    export KERNEL=$(basename "$i")
    cd $module_dir
    make clean
    make
    make clean
    cd -
done