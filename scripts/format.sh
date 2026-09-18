#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT/scripts/lint-image.sh" bash -c '
	set -euo pipefail

	clang-format -i src/*.cpp src/*.h tests/*.cpp

	gersemi -i CMakeLists.txt cmake

	shfmt -w scripts

	oxfmt .
'
