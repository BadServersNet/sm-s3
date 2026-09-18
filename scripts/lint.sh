#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT/scripts/lint-image.sh" bash -c '
	set -euo pipefail

	echo "==> clang-format"
	clang-format --dry-run -Werror src/*.cpp src/*.h tests/*.cpp

	echo "==> gersemi"
	gersemi --check CMakeLists.txt cmake

	echo "==> shfmt"
	shfmt -d scripts

	echo "==> shellcheck"
	shellcheck scripts/*.sh

	echo "==> oxfmt"
	oxfmt --check .

	echo "==> clang-tidy"
	clang-tidy --quiet src/*.cpp tests/*.cpp -- \
		-std=c++17 \
		-Isrc \
		-Ideps/sourcemod/public \
		-Ideps/sourcemod/public/amtl \
		-Ideps/sourcemod/public/amtl/amtl \
		-Ideps/sourcemod/sourcepawn/include \
		-Ideps/curl/include \
		-Ideps/mbedtls/include \
		-DCURL_STATICLIB \
		-DSM_S3_VERSION=\"lint\" \
		-Dstricmp=strcasecmp \
		-D_stricmp=strcasecmp \
		-D_snprintf=snprintf \
		-D_vsnprintf=vsnprintf \
		-DHAVE_STDINT_H \
		-DGNUC \
		-D_LINUX \
		-DPOSIX
'
