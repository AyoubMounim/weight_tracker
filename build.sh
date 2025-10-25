#!/bin/env bash

BUILD_DIR=build

[[ -d "$BUILD_DIR" ]] || mkdir -p "$BUILD_DIR"

gcc -ggdb main.c -o "$BUILD_DIR/wt" -lm
