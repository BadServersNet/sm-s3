#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${LINT_IMAGE:-sm-s3-lint}"

docker build -q -t "$IMAGE" -f "$ROOT/docker/lint.Dockerfile" "$ROOT/docker" >/dev/null

docker run --rm \
	-u "$(id -u):$(id -g)" \
	-e HOME=/tmp \
	-v "$ROOT":/src \
	-w /src \
	"$IMAGE" \
	"$@"
