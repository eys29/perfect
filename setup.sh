#!/bin/bash

set -e  # Exit on error

declare -a dirs=(
  "suite/wami/kernels/ser/debayer"
  "suite/wami/kernels/ser/lucas-kanade"
  "suite/pa1/kernels/ser/2d_convolution"
  "suite/pa1/kernels/ser/dwt53"
  "suite/pa1/kernels/ser/histogram_equalization"
)

for dir in "${dirs[@]}"; do
  echo "Building in $dir..."
  make -C "$dir" clean
  make -C "$dir" INPUT_SIZE=SMALL
done
