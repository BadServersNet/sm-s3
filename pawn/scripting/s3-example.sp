#include <sourcemod>
#include <s3>

#pragma newdecls required
#pragma semicolon 1

public Plugin myinfo =
{
	name = "S3 Example",
	author = "BuSheezy",
	description = "Exercises the S3 extension from the server console",
	version = "0.1.0",
	url = "https://github.com/BadServersNet/sm-s3"
};

ConVar gCV_Endpoint;
ConVar gCV_Bucket;
ConVar gCV_Region;
ConVar gCV_AccessKey;
ConVar gCV_SecretKey;
ConVar gCV_PathStyle;
ConVar gCV_PublicUrl;

S3Client gH_Client;

public void OnPluginStart()
{
	gCV_Endpoint = CreateConVar("s3_example_endpoint", "", "Endpoint host, e.g. http://127.0.0.1:9000");
	gCV_Bucket = CreateConVar("s3_example_bucket", "", "Bucket name");
	gCV_Region = CreateConVar("s3_example_region", "us-east-1", "SigV4 region");
	gCV_AccessKey = CreateConVar("s3_example_access_key", "", "Access key", FCVAR_PROTECTED);
	gCV_SecretKey = CreateConVar("s3_example_secret_key", "", "Secret key", FCVAR_PROTECTED);
	gCV_PathStyle = CreateConVar("s3_example_path_style", "1", "1 = path-style, 0 = virtual-hosted");
	gCV_PublicUrl = CreateConVar("s3_example_public_url", "", "Optional unsigned download base URL");

	RegServerCmd("sm_s3_head", Command_Head, "sm_s3_head <key>");
	RegServerCmd("sm_s3_put", Command_Put, "sm_s3_put <key> <path>");
	RegServerCmd("sm_s3_get", Command_Get, "sm_s3_get <key> <path>");
	RegServerCmd("sm_s3_delete", Command_Delete, "sm_s3_delete <key>");
	RegServerCmd("sm_s3_copy", Command_Copy, "sm_s3_copy <sourceKey> <destKey>");
	RegServerCmd("sm_s3_list", Command_List, "sm_s3_list [prefix]");
	RegServerCmd("sm_s3_presign", Command_Presign, "sm_s3_presign <key> [seconds]");
	RegServerCmd("sm_s3_cancel", Command_Cancel, "sm_s3_cancel <requestId>");

	AutoExecConfig(true, "s3-example");
}

public void OnConfigsExecuted()
{
	RebuildClient();
}

static void RebuildClient()
{
	delete gH_Client;

	char endpoint[256];
	char bucket[128];
	char region[64];
	char accessKey[128];
	char secretKey[128];
	char publicUrl[256];
	gCV_Endpoint.GetString(endpoint, sizeof(endpoint));
	gCV_Bucket.GetString(bucket, sizeof(bucket));
	gCV_Region.GetString(region, sizeof(region));
	gCV_AccessKey.GetString(accessKey, sizeof(accessKey));
	gCV_SecretKey.GetString(secretKey, sizeof(secretKey));
	gCV_PublicUrl.GetString(publicUrl, sizeof(publicUrl));

	if (endpoint[0] == '\0' || bucket[0] == '\0')
	{
		LogMessage("s3-example: set s3_example_endpoint and s3_example_bucket to enable the client");
		return;
	}

	gH_Client = new S3Client(endpoint, bucket, region, accessKey, secretKey);
	gH_Client.PathStyle = gCV_PathStyle.BoolValue;
	if (publicUrl[0] != '\0')
	{
		gH_Client.SetPublicUrl(publicUrl);
	}
}

static bool EnsureClient()
{
	if (gH_Client != null)
	{
		return true;
	}
	RebuildClient();
	if (gH_Client == null)
	{
		PrintToServer("s3-example: client is not configured");
		return false;
	}
	return true;
}

static void PrintResponse(const char[] what, S3Response response)
{
	char error[256];
	response.GetError(error, sizeof(error));
	char etag[128];
	response.GetETag(etag, sizeof(etag));
	PrintToServer("s3-example: %s -> status=%d http=%d length=%d etag=%s error=%s",
		what, response.Status, response.HttpStatus, response.ContentLength, etag, error);
}

public void OnRequestDone(S3Client client, S3Response response, any data)
{
	PrintResponse("request", response);
}

public void OnProgress(S3Client client, int transferred, int total, any data)
{
	if (total > 0)
	{
		PrintToServer("s3-example: progress %d / %d (%d%%)", transferred, total, transferred * 100 / total);
		return;
	}
	PrintToServer("s3-example: progress %d bytes", transferred);
}

public void OnListDone(S3Client client, S3Response response, S3ObjectList objects, const char[] nextToken, any data)
{
	PrintResponse("list", response);
	char key[512];
	char etag[128];
	for (int i = 0; i < objects.Length; i++)
	{
		objects.GetKey(i, key, sizeof(key));
		objects.GetETag(i, etag, sizeof(etag));
		PrintToServer("  %s  %d bytes  modified=%d  etag=%s", key, objects.GetSize(i), objects.GetLastModified(i), etag);
	}
	if (nextToken[0] != '\0')
	{
		PrintToServer("  next token: %s", nextToken);
	}
}

public Action Command_Head(int args)
{
	if (args < 1 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	char key[512];
	GetCmdArg(1, key, sizeof(key));
	int id = gH_Client.Head(key, OnRequestDone);
	PrintToServer("s3-example: head %s (request %d)", key, id);
	return Plugin_Handled;
}

public Action Command_Put(int args)
{
	if (args < 2 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	char key[512];
	char path[PLATFORM_MAX_PATH];
	GetCmdArg(1, key, sizeof(key));
	GetCmdArg(2, path, sizeof(path));
	int id = gH_Client.PutFile(key, path, OnRequestDone, 0, "application/octet-stream", OnProgress);
	PrintToServer("s3-example: put %s <- %s (request %d)", key, path, id);
	return Plugin_Handled;
}

public Action Command_Get(int args)
{
	if (args < 2 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	char key[512];
	char path[PLATFORM_MAX_PATH];
	GetCmdArg(1, key, sizeof(key));
	GetCmdArg(2, path, sizeof(path));
	int id = gH_Client.GetFile(key, path, OnRequestDone, 0, OnProgress);
	PrintToServer("s3-example: get %s -> %s (request %d)", key, path, id);
	return Plugin_Handled;
}

public Action Command_Delete(int args)
{
	if (args < 1 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	char key[512];
	GetCmdArg(1, key, sizeof(key));
	int id = gH_Client.Delete(key, OnRequestDone);
	PrintToServer("s3-example: delete %s (request %d)", key, id);
	return Plugin_Handled;
}

public Action Command_Copy(int args)
{
	if (args < 2 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	char source[512];
	char dest[512];
	GetCmdArg(1, source, sizeof(source));
	GetCmdArg(2, dest, sizeof(dest));
	int id = gH_Client.Copy(source, dest, OnRequestDone);
	PrintToServer("s3-example: copy %s -> %s (request %d)", source, dest, id);
	return Plugin_Handled;
}

public Action Command_List(int args)
{
	if (!EnsureClient())
	{
		return Plugin_Handled;
	}
	char prefix[512];
	if (args >= 1)
	{
		GetCmdArg(1, prefix, sizeof(prefix));
	}
	int id = gH_Client.List(prefix, OnListDone);
	PrintToServer("s3-example: list \"%s\" (request %d)", prefix, id);
	return Plugin_Handled;
}

public Action Command_Presign(int args)
{
	if (args < 1 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	char key[512];
	GetCmdArg(1, key, sizeof(key));
	int seconds = 3600;
	if (args >= 2)
	{
		seconds = GetCmdArgInt(2);
	}
	char url[1024];
	gH_Client.Presign(key, seconds, url, sizeof(url));
	PrintToServer("s3-example: %s", url);
	return Plugin_Handled;
}

public Action Command_Cancel(int args)
{
	if (args < 1 || !EnsureClient())
	{
		return Plugin_Handled;
	}
	int id = GetCmdArgInt(1);
	bool cancelled = gH_Client.Cancel(id);
	PrintToServer("s3-example: cancel %d -> %s", id, cancelled ? "ok" : "not found");
	return Plugin_Handled;
}
