#if defined _s3_included
 #endinput
#endif
#define _s3_included

enum S3Status
{
	S3Status_Ok = 0,
	S3Status_HttpError,
	S3Status_NetworkError,
	S3Status_Timeout,
	S3Status_IoError,
	S3Status_Cancelled,
};

enum S3Method
{
	S3Method_GET = 0,
	S3Method_PUT,
	S3Method_HEAD,
	S3Method_DELETE,
};

methodmap S3Response < Handle
{
	/**
	 * Outcome of the request after the client's retries were exhausted.
	 */
	property S3Status Status
	{
		public native get();
	}

	/**
	 * HTTP status code of the last attempt, or 0 if no response was received.
	 */
	property int HttpStatus
	{
		public native get();
	}

	/**
	 * Content-Length of the response, or -1 if unknown. For Head this is the object size.
	 */
	property int ContentLength
	{
		public native get();
	}

	/**
	 * Copies a human readable error. For S3 errors this is "<Code>: <Message>",
	 * for transport errors it is libcurl's description. Empty on success.
	 */
	public native void GetError(char[] buffer, int maxlength);

	/**
	 * Copies a response header value. Names are case-insensitive.
	 *
	 * @return  True if the header was present.
	 */
	public native bool GetHeader(const char[] name, char[] buffer, int maxlength);

	/**
	 * Copies the object's ETag without surrounding quotes, or an empty string.
	 */
	public native void GetETag(char[] buffer, int maxlength);
};

methodmap S3ObjectList < Handle
{
	property int Length
	{
		public native get();
	}

	public native void GetKey(int index, char[] buffer, int maxlength);
	public native int GetSize(int index);
	public native int GetLastModified(int index);
	public native void GetETag(int index, char[] buffer, int maxlength);
};

typeset S3Callback
{
	function void (S3Client client, S3Response response, any data);
};

typeset S3ProgressCallback
{
	function void (S3Client client, int transferred, int total, any data);
};

typeset S3ListCallback
{
	function void (S3Client client, S3Response response, S3ObjectList objects, const char[] nextToken, any data);
};

methodmap S3Client < Handle
{
	/**
	 * Creates a client for one bucket. Deleting the handle cancels every request
	 * still in flight for it without invoking their callbacks.
	 *
	 * @param endpoint   Host of the S3-compatible endpoint, optionally with a scheme,
	 *                   e.g. "<account>.r2.cloudflarestorage.com" or "http://127.0.0.1:9000".
	 * @param bucket     Bucket name.
	 * @param region     SigV4 region ("auto" for R2, "us-east-1" for MinIO/AWS default).
	 * @param accessKey  Access key id.
	 * @param secretKey  Secret access key.
	 */
	public native S3Client(const char[] endpoint, const char[] bucket, const char[] region, const char[] accessKey, const char[] secretKey);

	/**
	 * Path-style ("host/bucket/key", default) versus virtual-hosted ("bucket.host/key") addressing.
	 */
	property bool PathStyle
	{
		public native get();
		public native set(bool value);
	}

	/**
	 * Connection timeout in seconds. Default 10.
	 */
	property int ConnectTimeout
	{
		public native get();
		public native set(int value);
	}

	/**
	 * Stall timeout in seconds: a transfer that moves no bytes for this long is aborted. Default 60.
	 */
	property int Timeout
	{
		public native get();
		public native set(int value);
	}

	/**
	 * Retries per request for network errors, timeouts, 429 and 5xx responses. Default 3.
	 */
	property int MaxRetries
	{
		public native get();
		public native set(int value);
	}

	/**
	 * Upload speed cap in bytes per second, 0 for unlimited.
	 */
	property int MaxSendSpeed
	{
		public native get();
		public native set(int value);
	}

	/**
	 * Download speed cap in bytes per second, 0 for unlimited.
	 */
	property int MaxRecvSpeed
	{
		public native get();
		public native set(int value);
	}

	/**
	 * Base URL used for unsigned downloads (for example an R2 custom domain in front
	 * of a public bucket). When set, GetFile fetches "<baseUrl>/<key>" without signing.
	 */
	public native void SetPublicUrl(const char[] baseUrl);

	/**
	 * Uploads a local file with a streamed PUT.
	 *
	 * @param key          Object key.
	 * @param path         Local path relative to the game directory.
	 * @param callback     Invoked once on completion.
	 * @param data         Passed to the callbacks.
	 * @param contentType  Content-Type header.
	 * @param progress     Optional progress callback.
	 * @return             Request id usable with Cancel.
	 */
	public native int PutFile(const char[] key, const char[] path, S3Callback callback, any data = 0, const char[] contentType = "application/octet-stream", S3ProgressCallback progress = INVALID_FUNCTION);

	/**
	 * Downloads an object to a local file. Data is streamed into "<path>.part" and
	 * renamed onto path on success. With resume enabled an existing .part file is
	 * continued with a Range request.
	 *
	 * @return  Request id usable with Cancel.
	 */
	public native int GetFile(const char[] key, const char[] path, S3Callback callback, any data = 0, S3ProgressCallback progress = INVALID_FUNCTION, bool resume = true);

	public native int Head(const char[] key, S3Callback callback, any data = 0);
	public native int Delete(const char[] key, S3Callback callback, any data = 0);
	public native int Copy(const char[] sourceKey, const char[] destKey, S3Callback callback, any data = 0);

	/**
	 * Lists objects (ListObjectsV2). The S3ObjectList handle passed to the callback
	 * is freed after the callback returns.
	 */
	public native int List(const char[] prefix, S3ListCallback callback, any data = 0, int maxKeys = 1000, const char[] continuationToken = "");

	/**
	 * Cancels a request started by this plugin. The callback still runs with S3Status_Cancelled.
	 */
	public native bool Cancel(int requestId);

	/**
	 * Builds a presigned URL for the object without any network access.
	 */
	public native bool Presign(const char[] key, int expiresSeconds, char[] url, int maxlength, S3Method method = S3Method_GET);
};

public Extension __ext_s3 =
{
	name = "S3",
	file = "s3.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_s3_SetNTVOptional()
{
	MarkNativeAsOptional("S3Client.S3Client");
	MarkNativeAsOptional("S3Client.PathStyle.get");
	MarkNativeAsOptional("S3Client.PathStyle.set");
	MarkNativeAsOptional("S3Client.ConnectTimeout.get");
	MarkNativeAsOptional("S3Client.ConnectTimeout.set");
	MarkNativeAsOptional("S3Client.Timeout.get");
	MarkNativeAsOptional("S3Client.Timeout.set");
	MarkNativeAsOptional("S3Client.MaxRetries.get");
	MarkNativeAsOptional("S3Client.MaxRetries.set");
	MarkNativeAsOptional("S3Client.MaxSendSpeed.get");
	MarkNativeAsOptional("S3Client.MaxSendSpeed.set");
	MarkNativeAsOptional("S3Client.MaxRecvSpeed.get");
	MarkNativeAsOptional("S3Client.MaxRecvSpeed.set");
	MarkNativeAsOptional("S3Client.SetPublicUrl");
	MarkNativeAsOptional("S3Client.PutFile");
	MarkNativeAsOptional("S3Client.GetFile");
	MarkNativeAsOptional("S3Client.Head");
	MarkNativeAsOptional("S3Client.Delete");
	MarkNativeAsOptional("S3Client.Copy");
	MarkNativeAsOptional("S3Client.List");
	MarkNativeAsOptional("S3Client.Cancel");
	MarkNativeAsOptional("S3Client.Presign");
	MarkNativeAsOptional("S3Response.Status.get");
	MarkNativeAsOptional("S3Response.HttpStatus.get");
	MarkNativeAsOptional("S3Response.ContentLength.get");
	MarkNativeAsOptional("S3Response.GetError");
	MarkNativeAsOptional("S3Response.GetHeader");
	MarkNativeAsOptional("S3Response.GetETag");
	MarkNativeAsOptional("S3ObjectList.Length.get");
	MarkNativeAsOptional("S3ObjectList.GetKey");
	MarkNativeAsOptional("S3ObjectList.GetSize");
	MarkNativeAsOptional("S3ObjectList.GetLastModified");
	MarkNativeAsOptional("S3ObjectList.GetETag");
}
#endif
