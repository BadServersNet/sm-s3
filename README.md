# sm-s3

A SourceMod extension that talks to S3-compatible object storage (Cloudflare R2, MinIO, AWS S3, Backblaze B2, ...) with its own HTTP client. It signs requests with AWS Signature V4, streams uploads and downloads straight from and to disk, reports progress, resumes interrupted downloads and retries transient failures, all off the game thread.

## Features

- `PutFile`, `GetFile`, `Head`, `Delete`, `Copy`, `List` (ListObjectsV2) and presigned URLs
- SigV4 signing with `UNSIGNED-PAYLOAD`, path-style or virtual-hosted addressing
- Streamed transfers (memory use is independent of file size), progress callbacks, `Range` resume for downloads
- Automatic retries with exponential backoff for network errors, timeouts, `429` and `5xx`
- Readable errors: S3 `<Code>: <Message>` from the response body, or libcurl's description
- Optional unsigned public base URL for downloads (for example an R2 custom domain)
- Statically linked libcurl + mbedTLS + zlib; the only runtime dependencies are glibc 2.31+ and SourceMod 1.11+

## Installing

1. Download `sm-s3-<version>-linux.zip` from the releases page and extract it into `csgo/` (or your game directory). It contains `addons/sourcemod/extensions/s3.ext.so`, `addons/sourcemod/scripting/include/s3.inc` and `addons/sourcemod/configs/s3/ca-bundle.crt`.
2. The extension loads automatically when a plugin that includes `<s3>` loads. Check with `sm exts list`.

The binary is built in the Steam Runtime 3 (sniper) SDK, so the host needs glibc 2.31 or newer (Debian 11, Ubuntu 20.04 or newer, or any sniper-based container). Only Linux x86 is built at the moment.

Set the environment variable `SM_S3_VERBOSE=1` before starting the server to get libcurl's verbose output on the console.

## Usage

```sourcepawn
#include <s3>

S3Client client;

public void OnConfigsExecuted()
{
	delete client;
	client = new S3Client("<account>.r2.cloudflarestorage.com", "replays", "auto", accessKey, secretKey);
	client.MaxRetries = 3;
}

void Upload(const char[] key, const char[] path)
{
	client.PutFile(key, path, OnUploaded, 0, "application/octet-stream", OnProgress);
}

public void OnUploaded(S3Client c, S3Response response, any data)
{
	if (response.Status != S3Status_Ok)
	{
		char error[256];
		response.GetError(error, sizeof(error));
		LogError("upload failed (http %d): %s", response.HttpStatus, error);
	}
}

public void OnProgress(S3Client c, int transferred, int total, any data)
{
	PrintToServer("%d / %d", transferred, total);
}
```

Paths are relative to the game directory, as with SourceMod's own file natives. `GetFile` writes to `<path>.part` and renames it onto `path` when the download completes; call it again with `resume = true` to continue an interrupted download.

Handles passed to callbacks (`S3Response`, `S3ObjectList`) are freed when the callback returns. Deleting an `S3Client` cancels its in-flight requests silently; unloading a plugin cancels that plugin's requests.

See [`pawn/scripting/include/s3.inc`](pawn/scripting/include/s3.inc) for the full API and [`pawn/scripting/s3-example.sp`](pawn/scripting/s3-example.sp) for a console test plugin (`sm_s3_head`, `sm_s3_put`, `sm_s3_get`, `sm_s3_list`, ...).

## Building

Everything builds inside the Steam Runtime 3 (sniper) SDK container; nothing needs to be installed on the host besides Docker.

```bash
git clone --recursive https://github.com/BadServersNet/sm-s3
cd sm-s3
./scripts/build.sh
```

The packaged output lands in `build/package`. `scripts/minio.sh` starts a local MinIO container for testing (`endpoint=http://127.0.0.1:9000`, bucket `replays`, keys `minioadmin`/`minioadmin`, region `us-east-1`).

To build manually inside the container:

```bash
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/sniper-i386.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix build/package
```

## License

GPLv3, see [LICENSE](LICENSE).
