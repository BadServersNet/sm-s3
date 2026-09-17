#include <cstring>
#include <ctime>

#include "extension.h"
#include "sigv4.h"

static S3ClientObject *ReadClient(IPluginContext *pContext, cell_t handle)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	S3ClientObject *client;
	const HandleError error = handlesys->ReadHandle(handle, g_ClientType, &security, reinterpret_cast<void **>(&client));
	if (error != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid S3Client handle %x (error %d)", handle, error);
		return nullptr;
	}
	return client;
}

static S3ResponseObject *ReadResponse(IPluginContext *pContext, cell_t handle)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	S3ResponseObject *response;
	const HandleError error = handlesys->ReadHandle(handle, g_ResponseType, &security, reinterpret_cast<void **>(&response));
	if (error != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid S3Response handle %x (error %d)", handle, error);
		return nullptr;
	}
	return response;
}

static S3ObjectListObject *ReadObjectList(IPluginContext *pContext, cell_t handle)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());
	S3ObjectListObject *list;
	const HandleError error = handlesys->ReadHandle(handle, g_ObjectListType, &security, reinterpret_cast<void **>(&list));
	if (error != HandleError_None)
	{
		pContext->ThrowNativeError("Invalid S3ObjectList handle %x (error %d)", handle, error);
		return nullptr;
	}
	return list;
}

static std::string ReadString(IPluginContext *pContext, cell_t param)
{
	char *value;
	pContext->LocalToString(param, &value);
	return std::string(value);
}

static std::string ResolveGamePath(const std::string &relative)
{
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_Game, path, sizeof(path), "%s", relative.c_str());
	return std::string(path);
}

static IChangeableForward *CreateCallbackForward(IPluginContext *pContext, cell_t function, int paramCount, const ParamType *types)
{
	IPluginFunction *callback = pContext->GetFunctionById(function);
	if (callback == nullptr)
	{
		return nullptr;
	}
	IChangeableForward *forward = forwards->CreateForwardEx(nullptr, ET_Ignore, paramCount, types);
	if (forward == nullptr)
	{
		return nullptr;
	}
	forward->AddFunction(callback);
	return forward;
}

static IChangeableForward *CreateCompletedForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_Cell, Param_Cell };
	return CreateCallbackForward(pContext, function, 3, types);
}

static IChangeableForward *CreateListForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_Cell, Param_Cell, Param_String, Param_Cell };
	return CreateCallbackForward(pContext, function, 5, types);
}

static IChangeableForward *CreateProgressForward(IPluginContext *pContext, cell_t function)
{
	static const ParamType types[] = { Param_Cell, Param_Cell, Param_Cell, Param_Cell };
	return CreateCallbackForward(pContext, function, 4, types);
}

static bool ClientIsConfigured(IPluginContext *pContext, const S3ClientObject *client)
{
	const s3::ClientConfig &config = client->config;
	if (config.endpoint.host.empty() || config.bucket.empty())
	{
		pContext->ThrowNativeError("S3Client has no endpoint or bucket configured");
		return false;
	}
	return true;
}

static cell_t Submit(IPluginContext *pContext, S3ClientObject *client, JobSpec spec, cell_t completedFunction, cell_t data, cell_t progressFunction, bool listCallback)
{
	IChangeableForward *completed = listCallback ? CreateListForward(pContext, completedFunction) : CreateCompletedForward(pContext, completedFunction);
	if (completed == nullptr)
	{
		return pContext->ThrowNativeError("Invalid callback function");
	}

	JobRecord record;
	record.op = spec.op;
	record.clientHandle = client->handle;
	record.owner = pContext->GetIdentity();
	record.completed = completed;
	record.progress = progressFunction == -1 ? nullptr : CreateProgressForward(pContext, progressFunction);
	record.data = data;

	spec.config = client->config;
	return g_S3Extension.SubmitJob(std::move(spec), record);
}

static cell_t Native_S3Client_Create(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = new S3ClientObject();
	client->config.endpoint = s3::ParseEndpoint(ReadString(pContext, params[1]));
	client->config.bucket = ReadString(pContext, params[2]);
	client->config.region = ReadString(pContext, params[3]);
	client->config.accessKey = ReadString(pContext, params[4]);
	client->config.secretKey = ReadString(pContext, params[5]);
	if (client->config.region.empty())
	{
		client->config.region = "us-east-1";
	}

	HandleError error;
	const Handle_t handle = handlesys->CreateHandle(g_ClientType, client, pContext->GetIdentity(), myself->GetIdentity(), &error);
	if (handle == BAD_HANDLE)
	{
		delete client;
		return pContext->ThrowNativeError("Could not create S3Client handle (error %d)", error);
	}
	client->handle = handle;
	return handle;
}

static cell_t Native_S3Client_GetPathStyle(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	return client->config.pathStyle ? 1 : 0;
}

static cell_t Native_S3Client_SetPathStyle(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.pathStyle = params[2] != 0;
	return 1;
}

static cell_t Native_S3Client_GetConnectTimeout(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	return client == nullptr ? 0 : client->config.connectTimeout;
}

static cell_t Native_S3Client_SetConnectTimeout(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.connectTimeout = params[2];
	return 1;
}

static cell_t Native_S3Client_GetTimeout(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	return client == nullptr ? 0 : client->config.timeout;
}

static cell_t Native_S3Client_SetTimeout(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.timeout = params[2];
	return 1;
}

static cell_t Native_S3Client_GetMaxRetries(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	return client == nullptr ? 0 : client->config.maxRetries;
}

static cell_t Native_S3Client_SetMaxRetries(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.maxRetries = params[2];
	return 1;
}

static cell_t Native_S3Client_GetMaxSendSpeed(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	return client == nullptr ? 0 : static_cast<cell_t>(client->config.maxSendSpeed);
}

static cell_t Native_S3Client_SetMaxSendSpeed(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.maxSendSpeed = params[2];
	return 1;
}

static cell_t Native_S3Client_GetMaxRecvSpeed(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	return client == nullptr ? 0 : static_cast<cell_t>(client->config.maxRecvSpeed);
}

static cell_t Native_S3Client_SetMaxRecvSpeed(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.maxRecvSpeed = params[2];
	return 1;
}

static cell_t Native_S3Client_SetPublicUrl(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	client->config.publicUrl = ReadString(pContext, params[2]);
	return 1;
}

static cell_t Native_S3Client_PutFile(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	JobSpec spec;
	spec.op = S3Op::Put;
	spec.key = ReadString(pContext, params[2]);
	spec.filePath = ResolveGamePath(ReadString(pContext, params[3]));
	spec.contentType = ReadString(pContext, params[6]);
	if (spec.contentType.empty())
	{
		spec.contentType = "application/octet-stream";
	}
	return Submit(pContext, client, std::move(spec), params[4], params[5], params[7], false);
}

static cell_t Native_S3Client_GetFile(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	JobSpec spec;
	spec.op = S3Op::Get;
	spec.key = ReadString(pContext, params[2]);
	spec.filePath = ResolveGamePath(ReadString(pContext, params[3]));
	spec.resume = params[7] != 0;
	return Submit(pContext, client, std::move(spec), params[4], params[5], params[6], false);
}

static cell_t Native_S3Client_Head(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	JobSpec spec;
	spec.op = S3Op::Head;
	spec.key = ReadString(pContext, params[2]);
	return Submit(pContext, client, std::move(spec), params[3], params[4], -1, false);
}

static cell_t Native_S3Client_Delete(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	JobSpec spec;
	spec.op = S3Op::Delete;
	spec.key = ReadString(pContext, params[2]);
	return Submit(pContext, client, std::move(spec), params[3], params[4], -1, false);
}

static cell_t Native_S3Client_Copy(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	JobSpec spec;
	spec.op = S3Op::Copy;
	spec.sourceKey = ReadString(pContext, params[2]);
	spec.key = ReadString(pContext, params[3]);
	return Submit(pContext, client, std::move(spec), params[4], params[5], -1, false);
}

static cell_t Native_S3Client_List(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	JobSpec spec;
	spec.op = S3Op::List;
	spec.prefix = ReadString(pContext, params[2]);
	spec.maxKeys = params[5] > 0 ? params[5] : 1000;
	spec.continuationToken = ReadString(pContext, params[6]);
	return Submit(pContext, client, std::move(spec), params[3], params[4], -1, true);
}

static cell_t Native_S3Client_Cancel(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr)
	{
		return 0;
	}
	return g_S3Extension.CancelJob(params[2], pContext->GetIdentity()) ? 1 : 0;
}

static const char *PresignMethodName(cell_t method)
{
	switch (method)
	{
		case 1: return "PUT";
		case 2: return "HEAD";
		case 3: return "DELETE";
		default: return "GET";
	}
}

static cell_t Native_S3Client_Presign(IPluginContext *pContext, const cell_t *params)
{
	S3ClientObject *client = ReadClient(pContext, params[1]);
	if (client == nullptr || !ClientIsConfigured(pContext, client))
	{
		return 0;
	}
	const s3::ClientConfig &config = client->config;

	s3::SigningInput input;
	input.method = PresignMethodName(params[6]);
	input.host = s3::BuildHost(config);
	input.canonicalUri = s3::BuildObjectUri(config, ReadString(pContext, params[2]));

	s3::Credentials credentials;
	credentials.accessKey = config.accessKey;
	credentials.secretKey = config.secretKey;
	credentials.region = config.region;

	std::string amzDate;
	std::string dateStamp;
	s3::FormatAmzDate(time(nullptr), amzDate, dateStamp);
	const std::string query = s3::PresignQuery(input, credentials, amzDate, dateStamp, params[3]);
	const std::string url = config.endpoint.scheme + "://" + input.host + input.canonicalUri + "?" + query;

	pContext->StringToLocalUTF8(params[4], params[5], url.c_str(), nullptr);
	return 1;
}

static cell_t Native_S3Response_GetStatus(IPluginContext *pContext, const cell_t *params)
{
	S3ResponseObject *response = ReadResponse(pContext, params[1]);
	return response == nullptr ? 0 : static_cast<cell_t>(response->status);
}

static cell_t Native_S3Response_GetHttpStatus(IPluginContext *pContext, const cell_t *params)
{
	S3ResponseObject *response = ReadResponse(pContext, params[1]);
	return response == nullptr ? 0 : static_cast<cell_t>(response->httpStatus);
}

static cell_t Native_S3Response_GetContentLength(IPluginContext *pContext, const cell_t *params)
{
	S3ResponseObject *response = ReadResponse(pContext, params[1]);
	return response == nullptr ? -1 : ClampToCell(response->contentLength);
}

static cell_t Native_S3Response_GetError(IPluginContext *pContext, const cell_t *params)
{
	S3ResponseObject *response = ReadResponse(pContext, params[1]);
	if (response == nullptr)
	{
		return 0;
	}
	pContext->StringToLocalUTF8(params[2], params[3], response->error.c_str(), nullptr);
	return 1;
}

static std::string ToLowerCopy(const std::string &value)
{
	std::string out = value;
	for (char &c : out)
	{
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return out;
}

static cell_t Native_S3Response_GetHeader(IPluginContext *pContext, const cell_t *params)
{
	S3ResponseObject *response = ReadResponse(pContext, params[1]);
	if (response == nullptr)
	{
		return 0;
	}
	const std::string name = ToLowerCopy(ReadString(pContext, params[2]));
	const auto it = response->headers.find(name);
	if (it == response->headers.end())
	{
		return 0;
	}
	pContext->StringToLocalUTF8(params[3], params[4], it->second.c_str(), nullptr);
	return 1;
}

static std::string StripQuotes(const std::string &value)
{
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
	{
		return value.substr(1, value.size() - 2);
	}
	return value;
}

static cell_t Native_S3Response_GetETag(IPluginContext *pContext, const cell_t *params)
{
	S3ResponseObject *response = ReadResponse(pContext, params[1]);
	if (response == nullptr)
	{
		return 0;
	}
	const auto it = response->headers.find("etag");
	const std::string etag = it == response->headers.end() ? "" : StripQuotes(it->second);
	pContext->StringToLocalUTF8(params[2], params[3], etag.c_str(), nullptr);
	return 1;
}

static const s3::ListedObject *ReadListedObject(IPluginContext *pContext, const cell_t *params)
{
	S3ObjectListObject *list = ReadObjectList(pContext, params[1]);
	if (list == nullptr)
	{
		return nullptr;
	}
	const cell_t index = params[2];
	if (index < 0 || static_cast<size_t>(index) >= list->objects.size())
	{
		pContext->ThrowNativeError("Index %d is out of bounds (size %d)", index, static_cast<int>(list->objects.size()));
		return nullptr;
	}
	return &list->objects[index];
}

static cell_t Native_S3ObjectList_GetLength(IPluginContext *pContext, const cell_t *params)
{
	S3ObjectListObject *list = ReadObjectList(pContext, params[1]);
	return list == nullptr ? 0 : static_cast<cell_t>(list->objects.size());
}

static cell_t Native_S3ObjectList_GetKey(IPluginContext *pContext, const cell_t *params)
{
	const s3::ListedObject *object = ReadListedObject(pContext, params);
	if (object == nullptr)
	{
		return 0;
	}
	pContext->StringToLocalUTF8(params[3], params[4], object->key.c_str(), nullptr);
	return 1;
}

static cell_t Native_S3ObjectList_GetSize(IPluginContext *pContext, const cell_t *params)
{
	const s3::ListedObject *object = ReadListedObject(pContext, params);
	return object == nullptr ? 0 : ClampToCell(object->size);
}

static cell_t Native_S3ObjectList_GetLastModified(IPluginContext *pContext, const cell_t *params)
{
	const s3::ListedObject *object = ReadListedObject(pContext, params);
	return object == nullptr ? 0 : ClampToCell(object->lastModified);
}

static cell_t Native_S3ObjectList_GetETag(IPluginContext *pContext, const cell_t *params)
{
	const s3::ListedObject *object = ReadListedObject(pContext, params);
	if (object == nullptr)
	{
		return 0;
	}
	pContext->StringToLocalUTF8(params[3], params[4], StripQuotes(object->etag).c_str(), nullptr);
	return 1;
}

const sp_nativeinfo_t g_Natives[] =
{
	{ "S3Client.S3Client", Native_S3Client_Create },
	{ "S3Client.PathStyle.get", Native_S3Client_GetPathStyle },
	{ "S3Client.PathStyle.set", Native_S3Client_SetPathStyle },
	{ "S3Client.ConnectTimeout.get", Native_S3Client_GetConnectTimeout },
	{ "S3Client.ConnectTimeout.set", Native_S3Client_SetConnectTimeout },
	{ "S3Client.Timeout.get", Native_S3Client_GetTimeout },
	{ "S3Client.Timeout.set", Native_S3Client_SetTimeout },
	{ "S3Client.MaxRetries.get", Native_S3Client_GetMaxRetries },
	{ "S3Client.MaxRetries.set", Native_S3Client_SetMaxRetries },
	{ "S3Client.MaxSendSpeed.get", Native_S3Client_GetMaxSendSpeed },
	{ "S3Client.MaxSendSpeed.set", Native_S3Client_SetMaxSendSpeed },
	{ "S3Client.MaxRecvSpeed.get", Native_S3Client_GetMaxRecvSpeed },
	{ "S3Client.MaxRecvSpeed.set", Native_S3Client_SetMaxRecvSpeed },
	{ "S3Client.SetPublicUrl", Native_S3Client_SetPublicUrl },
	{ "S3Client.PutFile", Native_S3Client_PutFile },
	{ "S3Client.GetFile", Native_S3Client_GetFile },
	{ "S3Client.Head", Native_S3Client_Head },
	{ "S3Client.Delete", Native_S3Client_Delete },
	{ "S3Client.Copy", Native_S3Client_Copy },
	{ "S3Client.List", Native_S3Client_List },
	{ "S3Client.Cancel", Native_S3Client_Cancel },
	{ "S3Client.Presign", Native_S3Client_Presign },
	{ "S3Response.Status.get", Native_S3Response_GetStatus },
	{ "S3Response.HttpStatus.get", Native_S3Response_GetHttpStatus },
	{ "S3Response.ContentLength.get", Native_S3Response_GetContentLength },
	{ "S3Response.GetError", Native_S3Response_GetError },
	{ "S3Response.GetHeader", Native_S3Response_GetHeader },
	{ "S3Response.GetETag", Native_S3Response_GetETag },
	{ "S3ObjectList.Length.get", Native_S3ObjectList_GetLength },
	{ "S3ObjectList.GetKey", Native_S3ObjectList_GetKey },
	{ "S3ObjectList.GetSize", Native_S3ObjectList_GetSize },
	{ "S3ObjectList.GetLastModified", Native_S3ObjectList_GetLastModified },
	{ "S3ObjectList.GetETag", Native_S3ObjectList_GetETag },
	{ nullptr, nullptr },
};
