#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "s3job.h"

class TransferThread
{
public:
	bool Start();
	void Stop();
	void Enqueue(std::unique_ptr<S3Job> job);
	void Cancel(int jobId);

private:
	void Run();
	void AdmitIncoming();
	void ApplyCancellations();
	void ReapCompleted();
	void StartDueRetries();
	long ComputeWaitMs() const;
	void RemoveFinished();

	std::thread m_thread;
	CURLM *m_multi = nullptr;
	std::atomic<bool> m_running { false };
	std::mutex m_mutex;
	std::vector<std::unique_ptr<S3Job>> m_incoming;
	std::vector<int> m_cancelRequests;
	std::vector<std::unique_ptr<S3Job>> m_active;
};
