#!/usr/bin/env bash
set -euo pipefail

NAME="${MINIO_CONTAINER:-sm-s3-minio}"
ROOT_USER="${MINIO_ROOT_USER:-minioadmin}"
ROOT_PASSWORD="${MINIO_ROOT_PASSWORD:-minioadmin}"
BUCKET="${MINIO_BUCKET:-replays}"

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" \
	-p 9000:9000 -p 9001:9001 \
	-e MINIO_ROOT_USER="$ROOT_USER" \
	-e MINIO_ROOT_PASSWORD="$ROOT_PASSWORD" \
	quay.io/minio/minio server /data --console-address ":9001" >/dev/null

until docker exec "$NAME" mc alias set local http://127.0.0.1:9000 "$ROOT_USER" "$ROOT_PASSWORD" >/dev/null 2>&1; do
	sleep 1
done
docker exec "$NAME" mc mb --ignore-existing "local/$BUCKET" >/dev/null

echo "MinIO running at http://127.0.0.1:9000 (console :9001)"
echo "endpoint=127.0.0.1:9000 bucket=$BUCKET region=us-east-1 access=$ROOT_USER secret=$ROOT_PASSWORD (use http, path-style)"
