#!/usr/bin/env bash
set -euo pipefail

IMAGE="${SNIPER_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"

docker run --rm \
	-u "$(id -u):$(id -g)" \
	-v "$ROOT":/src \
	-w /src \
	"$IMAGE" \
	bash -c "
		set -euo pipefail
		cmake -S . -B build -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE=cmake/sniper-i386.cmake \
			-DCMAKE_BUILD_TYPE=$BUILD_TYPE
		cmake --build build
		ctest --test-dir build --output-on-failure
		cmake --install build --prefix build/package
	"
