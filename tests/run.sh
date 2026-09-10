#!/usr/bin/env bash
set -euo pipefail

cc \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -pedantic \
  -Iinclude \
  src/vector_accel_core.c \
  tests/test_vector_accel.c \
  -o tests/test_vector_accel

./tests/test_vector_accel
rm -f tests/test_vector_accel
