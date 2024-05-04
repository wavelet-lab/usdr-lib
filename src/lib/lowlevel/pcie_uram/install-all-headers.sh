#!/usr/bin/env bash

set -e

#!/bin/bash
for i in $(apt-cache search linux-headers | awk '{ print $1}'); do
    apt-get install -y $i
done
