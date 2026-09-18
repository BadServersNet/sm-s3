#include "extension.h"

#include <cstdlib>
#include <iterator>

#include <curl/curl.h>

S3Extension g_S3Extension;
MainThreadQueue g_MainQueue;
TransferThread g_TransferThread;
HandleType_t g_ClientType = 0;
HandleType_t g_ResponseType = 0;
HandleType_t g_ObjectListType = 0;

SMEXT_LINK(&g_S3Extension);

static const char kSystemCaBundle[] = "/etc/ssl/certs/ca-certificates.crt";

static void OnGameFrame(bool)
{
	if (!g_MainQueue.HasEvents())
	{
		return;
	}

	const std::vector<TransferEvent> events = g_MainQueue.Drain();

	for (const TransferEvent &event : events)
	{
		g_S3Extension.HandleEvent(event);
	}
}

bool S3Extension::SDK_OnLoad(char *error, size_t maxlength, bool)
{
	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
	{
		ke::SafeStrcpy(error, maxlength, "curl_global_init failed");

		return false;
	}

	HandleError handleError;
	g_ClientType = handlesys->CreateType("S3Client", this, 0, nullptr, nullptr, myself->GetIdentity(), &handleError);

	if (g_ClientType == 0)
	{
		ke::SafeSprintf(error, maxlength, "Could not create S3Client handle type (error %d)", handleError);

		return false;
	}

	g_ResponseType =
		handlesys->CreateType("S3Response", this, 0, nullptr, nullptr, myself->GetIdentity(), &handleError);

	if (g_ResponseType == 0)
	{
		ke::SafeSprintf(error, maxlength, "Could not create S3Response handle type (error %d)", handleError);

		return false;
	}

	g_ObjectListType =
		handlesys->CreateType("S3ObjectList", this, 0, nullptr, nullptr, myself->GetIdentity(), &handleError);

	if (g_ObjectListType == 0)
	{
		ke::SafeSprintf(error, maxlength, "Could not create S3ObjectList handle type (error %d)", handleError);

		return false;
	}

	DetectCaBundle();
	const char *verbose = getenv("SM_S3_VERBOSE");
	m_verbose = verbose != nullptr && verbose[0] == '1';

	if (!g_TransferThread.Start())
	{
		ke::SafeStrcpy(error, maxlength, "Could not start the transfer thread");

		return false;
	}

	sharesys->AddNatives(myself, g_Natives);
	sharesys->RegisterLibrary(myself, "s3");
	plsys->AddPluginsListener(this);
	smutils->AddGameFrameHook(OnGameFrame);

	return true;
}

void S3Extension::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(OnGameFrame);
	plsys->RemovePluginsListener(this);
	g_MainQueue.SetEnabled(false);
	g_TransferThread.Stop();
	g_MainQueue.Drain();

	for (auto &entry : m_jobs)
	{
		ReleaseRecord(entry.second);
	}

	m_jobs.clear();

	handlesys->RemoveType(g_ObjectListType, myself->GetIdentity());
	handlesys->RemoveType(g_ResponseType, myself->GetIdentity());
	handlesys->RemoveType(g_ClientType, myself->GetIdentity());
	curl_global_cleanup();
}

void S3Extension::DetectCaBundle()
{
	char path[PLATFORM_MAX_PATH];
	smutils->BuildPath(Path_SM, path, sizeof(path), "configs/s3/ca-bundle.crt");

	if (libsys->PathExists(path))
	{
		m_caBundlePath = path;

		return;
	}

	if (libsys->PathExists(kSystemCaBundle))
	{
		m_caBundlePath = kSystemCaBundle;

		return;
	}

	smutils->LogError(myself, "No CA bundle found at %s or %s; HTTPS requests will fail", path, kSystemCaBundle);
}

void S3Extension::OnHandleDestroy(HandleType_t type, void *object)
{
	if (type == g_ClientType)
	{
		S3ClientObject *client = static_cast<S3ClientObject *>(object);
		CancelJobsForClient(client->handle);
		delete client;

		return;
	}

	if (type == g_ResponseType)
	{
		delete static_cast<S3ResponseObject *>(object);

		return;
	}

	if (type == g_ObjectListType)
	{
		delete static_cast<S3ObjectListObject *>(object);
	}
}

void S3Extension::OnPluginUnloaded(IPlugin *plugin)
{
	IdentityToken_t *identity = plugin->GetIdentity();

	for (auto it = m_jobs.begin(); it != m_jobs.end();)
	{
		if (it->second.owner != identity)
		{
			++it;
			continue;
		}

		it = DropJob(it);
	}
}

void S3Extension::CancelJobsForClient(Handle_t clientHandle)
{
	for (auto it = m_jobs.begin(); it != m_jobs.end();)
	{
		if (it->second.clientHandle != clientHandle)
		{
			++it;
			continue;
		}

		it = DropJob(it);
	}
}

S3Extension::JobMap::iterator S3Extension::DropJob(JobMap::iterator it)
{
	g_TransferThread.Cancel(it->first);

	if (it->second.dispatching)
	{
		it->second.dropped = true;

		return std::next(it);
	}

	ReleaseRecord(it->second);

	return m_jobs.erase(it);
}

void S3Extension::ReleaseRecord(JobRecord &record)
{
	if (record.completed != nullptr)
	{
		forwards->ReleaseForward(record.completed);
		record.completed = nullptr;
	}

	if (record.progress != nullptr)
	{
		forwards->ReleaseForward(record.progress);
		record.progress = nullptr;
	}
}

int S3Extension::SubmitJob(JobSpec spec, JobRecord record)
{
	const int id = m_nextJobId++;
	spec.id = id;
	spec.caBundle = m_caBundlePath;
	spec.verbose = m_verbose;
	record.id = id;
	m_jobs[id] = record;
	g_TransferThread.Enqueue(std::make_unique<S3Job>(std::move(spec), &g_MainQueue));

	return id;
}

bool S3Extension::CancelJob(int jobId, IdentityToken_t *requester)
{
	auto it = m_jobs.find(jobId);

	if (it == m_jobs.end())
	{
		return false;
	}

	if (it->second.owner != requester)
	{
		return false;
	}

	g_TransferThread.Cancel(jobId);

	return true;
}

void S3Extension::HandleEvent(const TransferEvent &event)
{
	auto it = m_jobs.find(event.jobId);

	if (it == m_jobs.end())
	{
		return;
	}

	if (event.kind == TransferEvent::Kind::Progress)
	{
		HandleProgress(it->second, event);

		return;
	}

	JobRecord record = it->second;
	m_jobs.erase(it);
	DispatchComplete(record, event);
	ReleaseRecord(record);
}

void S3Extension::HandleProgress(JobRecord &record, const TransferEvent &event)
{
	record.dispatching = true;
	DispatchProgress(record, event);
	record.dispatching = false;

	if (!record.dropped)
	{
		return;
	}

	ReleaseRecord(record);
	m_jobs.erase(event.jobId);
}

void S3Extension::DispatchProgress(const JobRecord &record, const TransferEvent &event)
{
	if (record.progress == nullptr || record.progress->GetFunctionCount() == 0)
	{
		return;
	}

	record.progress->PushCell(record.clientHandle);
	record.progress->PushCell(ClampToCell(event.transferred));
	record.progress->PushCell(ClampToCell(event.total));
	record.progress->PushCell(record.data);
	record.progress->Execute(nullptr);
}

void S3Extension::FreeOwnedHandle(Handle_t handle, IdentityToken_t *owner)
{
	if (handle == BAD_HANDLE)
	{
		return;
	}

	HandleSecurity const security(owner, myself->GetIdentity());
	handlesys->FreeHandle(handle, &security);
}

Handle_t S3Extension::CreateResponseHandle(const JobRecord &record, const TransferEvent &event)
{
	S3ResponseObject *response = new S3ResponseObject();
	response->status = event.status;
	response->httpStatus = event.httpStatus;
	response->contentLength = event.contentLength;
	response->error = event.error;
	response->headers = event.headers;

	HandleError handleError;
	const Handle_t handle =
		handlesys->CreateHandle(g_ResponseType, response, record.owner, myself->GetIdentity(), &handleError);

	if (handle == BAD_HANDLE)
	{
		delete response;
		smutils->LogError(myself, "Could not create S3Response handle (error %d)", handleError);
	}

	return handle;
}

Handle_t
S3Extension::CreateObjectListHandle(const JobRecord &record, const TransferEvent &event, std::string &nextToken)
{
	S3ObjectListObject *list = new S3ObjectListObject();

	if (event.status == S3Status::Ok)
	{
		s3::ListResult result = s3::ParseListObjects(event.body);
		list->objects = std::move(result.objects);
		nextToken = result.nextToken;
	}

	HandleError handleError;
	const Handle_t handle =
		handlesys->CreateHandle(g_ObjectListType, list, record.owner, myself->GetIdentity(), &handleError);

	if (handle == BAD_HANDLE)
	{
		delete list;
		smutils->LogError(myself, "Could not create S3ObjectList handle (error %d)", handleError);
	}

	return handle;
}

void S3Extension::DispatchComplete(JobRecord &record, const TransferEvent &event)
{
	if (record.completed == nullptr || record.completed->GetFunctionCount() == 0)
	{
		return;
	}

	const Handle_t responseHandle = CreateResponseHandle(record, event);

	if (responseHandle == BAD_HANDLE)
	{
		return;
	}

	if (record.op == S3Op::List)
	{
		std::string nextToken;
		const Handle_t listHandle = CreateObjectListHandle(record, event, nextToken);

		if (listHandle == BAD_HANDLE)
		{
			FreeOwnedHandle(responseHandle, record.owner);

			return;
		}

		record.completed->PushCell(record.clientHandle);
		record.completed->PushCell(responseHandle);
		record.completed->PushCell(listHandle);
		record.completed->PushString(nextToken.c_str());
		record.completed->PushCell(record.data);
		record.completed->Execute(nullptr);
		FreeOwnedHandle(listHandle, record.owner);
		FreeOwnedHandle(responseHandle, record.owner);

		return;
	}

	record.completed->PushCell(record.clientHandle);
	record.completed->PushCell(responseHandle);
	record.completed->PushCell(record.data);
	record.completed->Execute(nullptr);
	FreeOwnedHandle(responseHandle, record.owner);
}
