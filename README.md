# sm-s3

[![CI](https://github.com/BadServersNet/sm-s3/actions/workflows/ci.yml/badge.svg)](https://github.com/BadServersNet/sm-s3/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/BadServersNet/sm-s3)](https://github.com/BadServersNet/sm-s3/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

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

1. Download `sm-s3-<version>-linux.zip` from the [releases page](https://github.com/BadServersNet/sm-s3/releases) (or the `sm-s3-linux` artifact of the latest [CI run](https://github.com/BadServersNet/sm-s3/actions)) and extract it into `csgo/` (or your game directory). It contains `addons/sourcemod/extensions/s3.ext.so`, `addons/sourcemod/scripting/include/s3.inc` and `addons/sourcemod/configs/s3/ca-bundle.crt`.
2. The extension loads automatically when a plugin that includes `<s3>` loads. Check with `sm exts list`.
3. Plugin authors: copy `s3.inc` into your `scripting/include` directory and `#include <s3>`.

The binary is built in the Steam Runtime 3 (sniper) SDK, so the host needs glibc 2.31 or newer (Debian 11, Ubuntu 20.04 or newer, or any sniper-based container). Only Linux x86 is built at the moment.

Set the environment variable `SM_S3_VERBOSE=1` before starting the server to get libcurl's verbose output on the console.

## Providers

| Provider      | Endpoint                             | Region              | `PathStyle`      |
| ------------- | ------------------------------------ | ------------------- | ---------------- |
| Cloudflare R2 | `<account>.r2.cloudflarestorage.com` | `auto`              | `true` (default) |
| AWS S3        | `s3.<region>.amazonaws.com`          | the bucket's region | `false`          |
| Backblaze B2  | `s3.<region>.backblazeb2.com`        | e.g. `us-west-004`  | `true` (default) |
| MinIO         | `http://<host>:9000`                 | `us-east-1`         | `true` (default) |

The endpoint defaults to `https://` when no scheme is given. An empty region falls back to `us-east-1`.

## API

| Native                                                                                 | What it does                                                                                   |
| -------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `S3Client(endpoint, bucket, region, accessKey, secretKey)`                             | Creates a client. Region is `auto` for R2, `us-east-1` for MinIO/AWS defaults.                 |
| `PathStyle`, `ConnectTimeout`, `Timeout`, `MaxRetries`, `MaxSendSpeed`, `MaxRecvSpeed` | Properties. `Timeout` is a stall timeout, so large transfers never time out while progressing. |
| `SetPublicUrl(baseUrl)`                                                                | Downloads fetch `<baseUrl>/<key>` unsigned, e.g. through an R2 custom domain.                  |
| `PutFile(key, path, callback, data, contentType, progress)`                            | Streamed PUT from disk.                                                                        |
| `GetFile(key, path, callback, data, progress, resume)`                                 | Streamed download to `<path>.part`, renamed on success, resumable.                             |
| `Head(key, ...)`, `Delete(key, ...)`, `Copy(src, dst, ...)`                            | Single-object operations.                                                                      |
| `List(prefix, callback, data, maxKeys, token)`                                         | ListObjectsV2; the callback receives an `S3ObjectList`.                                        |
| `Cancel(requestId)`                                                                    | Cancels a request; its callback runs with `S3Status_Cancelled`.                                |
| `Presign(key, seconds, url, maxlength, method)`                                        | Builds a presigned URL locally, no network.                                                    |

`S3Response` exposes `Status` (`S3Status_Ok`, `HttpError`, `NetworkError`, `Timeout`, `IoError`, `Cancelled`), `HttpStatus`, `ContentLength`, `GetError`, `GetHeader` and `GetETag`.

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

Downloading with resume, listing every page under a prefix, and presigning a link:

```sourcepawn
void Download(const char[] key, const char[] path)
{
	client.GetFile(key, path, OnDownloaded, 0, INVALID_FUNCTION, true);
}

public void OnDownloaded(S3Client c, S3Response response, any data)
{
	if (response.Status == S3Status_Ok)
	{
		PrintToServer("downloaded %d bytes", response.ContentLength);
	}
}

void ListReplays(const char[] token = "")
{
	client.List("replays/", OnListed, 0, 1000, token);
}

public void OnListed(S3Client c, S3Response response, S3ObjectList objects, const char[] nextToken, any data)
{
	if (response.Status != S3Status_Ok)
	{
		return;
	}

	char key[512];

	for (int i = 0; i < objects.Length; i++)
	{
		objects.GetKey(i, key, sizeof(key));
		PrintToServer("%s (%d bytes)", key, objects.GetSize(i));
	}

	if (nextToken[0] != '\0')
	{
		ListReplays(nextToken);
	}
}

void ShareLink(const char[] key)
{
	char url[1024];
	client.Presign(key, 3600, url, sizeof(url));
	PrintToServer("%s", url);
}
```

Paths are relative to the game directory, as with SourceMod's own file natives. `GetFile` writes to `<path>.part` and renames it onto `path` when the download completes; call it again with `resume = true` to continue an interrupted download.

Handles passed to callbacks (`S3Response`, `S3ObjectList`) are freed when the callback returns. Deleting an `S3Client` cancels its in-flight requests silently; unloading a plugin cancels that plugin's requests.

## Limits

- Uploads are a single `PUT` (no multipart), so the provider's single-request limit applies (5 GB on AWS S3 and R2).
- Sizes and progress values are SourcePawn cells and clamp at 2 GB - 1.
- `List` returns at most 1000 keys per page; follow `nextToken` for more. Response bodies over 4 MB are truncated.
- Presigned URLs are valid for 1 second to 7 days (`604800`).
- Retries apply to network errors, timeouts, `429` and `5xx`. Other `4xx` responses fail immediately.

Keep credentials out of public configs: store them in a `FCVAR_PROTECTED` convar or a file the web server does not expose.

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

## Formatting and linting

Formatters and linters run in a pinned tooling container (`docker/lint.Dockerfile`), so only Docker is required. Every formatter uses a line width of 120.

```bash
./scripts/format.sh
```

```bash
./scripts/lint.sh
```

| Files                | Formatter      | Linter                                  |
| -------------------- | -------------- | --------------------------------------- |
| C++ (`src`, `tests`) | `clang-format` | `clang-tidy` (warnings are errors)      |
| CMake                | `gersemi`      |                                         |
| Shell (`scripts`)    | `shfmt`        | `shellcheck`                            |
| Markdown, YAML, JSON | `oxfmt`        |                                         |
| SourcePawn (`pawn`)  |                | `spcomp -E` in CI (warnings are errors) |

`clang-tidy` enforces a cognitive complexity limit of 15 per function. CI runs `scripts/lint.sh` on every push and pull request.

Separate logical steps with blank lines: after a closing brace, and before `if`, `for`, `while`, `switch` and `return`. `clang-format` keeps those blank lines but cannot add them.

## Releasing

Pushing a tag that starts with `v` (for example `v0.1.0`) runs the CI build and attaches `sm-s3-<tag>-linux.zip` to a GitHub release. Bump `SM_S3_VERSION` in `CMakeLists.txt` first so `sm exts list` reports the right version.

## License

GPLv3, see [LICENSE](LICENSE).
