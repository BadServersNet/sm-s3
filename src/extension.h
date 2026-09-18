#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "smsdk_ext.h"

#include "main_thread_queue.h"
#include "s3client.h"
#include "s3job.h"
#include "transfer_thread.h"
#include "xml.h"

struct S3ClientObject
{
	s3::ClientConfig config;
	Handle_t handle = BAD_HANDLE;
};

struct S3ResponseObject
{
	S3Status status = S3Status::Ok;
	long httpStatus = 0;
	long long contentLength = -1;
	std::string error;
	std::map<std::string, std::string> headers;
};

struct S3ObjectListObject
{
	std::vector<s3::ListedObject> objects;
};

struct JobRecord
{
	int id = 0;
	S3Op op = S3Op::Head;
	Handle_t clientHandle = BAD_HANDLE;
	IdentityToken_t *owner = nullptr;
	IChangeableForward *completed = nullptr;
	IChangeableForward *progress = nullptr;
	cell_t data = 0;
	bool dispatching = false;
	bool dropped = false;
};

class S3Extension : public SDKExtension, public IHandleTypeDispatch, public IPluginsListener
{
public:
	bool SDK_OnLoad(char *error, size_t maxlength, bool late) override;
	void SDK_OnUnload() override;

	void OnHandleDestroy(HandleType_t type, void *object) override;
	void OnPluginUnloaded(IPlugin *plugin) override;

	int SubmitJob(JobSpec spec, JobRecord record);
	bool CancelJob(int jobId, IdentityToken_t *requester);
	void HandleEvent(const TransferEvent &event);

	const std::string &CaBundlePath() const { return m_caBundlePath; }

	bool Verbose() const { return m_verbose; }

private:
	using JobMap = std::unordered_map<int, JobRecord>;

	void CancelJobsForClient(Handle_t clientHandle);
	JobMap::iterator DropJob(JobMap::iterator it);
	void HandleProgress(JobRecord &record, const TransferEvent &event);
	void ReleaseRecord(JobRecord &record);
	void DispatchProgress(const JobRecord &record, const TransferEvent &event);
	void DispatchComplete(JobRecord &record, const TransferEvent &event);
	Handle_t CreateResponseHandle(const JobRecord &record, const TransferEvent &event);
	Handle_t CreateObjectListHandle(const JobRecord &record, const TransferEvent &event, std::string &nextToken);
	void FreeOwnedHandle(Handle_t handle, IdentityToken_t *owner);
	void DetectCaBundle();

	JobMap m_jobs;
	int m_nextJobId = 1;
	std::string m_caBundlePath;
	bool m_verbose = false;
};

extern S3Extension g_S3Extension;
extern MainThreadQueue g_MainQueue;
extern TransferThread g_TransferThread;
extern HandleType_t g_ClientType;
extern HandleType_t g_ResponseType;
extern HandleType_t g_ObjectListType;
extern const sp_nativeinfo_t g_Natives[];

static inline cell_t ClampToCell(long long value)
{
	if (value > 0x7FFFFFFFLL)
	{
		return 0x7FFFFFFF;
	}

	if (value < -0x80000000LL)
	{
		return static_cast<cell_t>(-0x80000000LL);
	}

	return static_cast<cell_t>(value);
}
