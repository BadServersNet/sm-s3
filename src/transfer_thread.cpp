#include "transfer_thread.h"

#include <algorithm>
#include <chrono>

static const long kMaxWaitMs = 1000;

bool TransferThread::Start()
{
	m_multi = curl_multi_init();

	if (m_multi == nullptr)
	{
		return false;
	}

	m_running = true;
	m_thread = std::thread(&TransferThread::Run, this);

	return true;
}

void TransferThread::Stop()
{
	if (!m_running)
	{
		return;
	}

	m_running = false;
	curl_multi_wakeup(m_multi);
	m_thread.join();

	for (auto &job : m_active)
	{
		job->Cancel(m_multi);
	}

	m_active.clear();
	m_incoming.clear();
	curl_multi_cleanup(m_multi);
	m_multi = nullptr;
}

void TransferThread::Enqueue(std::unique_ptr<S3Job> job)
{
	{
		std::lock_guard<std::mutex> const lock(m_mutex);
		m_incoming.push_back(std::move(job));
	}

	curl_multi_wakeup(m_multi);
}

void TransferThread::Cancel(int jobId)
{
	{
		std::lock_guard<std::mutex> const lock(m_mutex);
		m_cancelRequests.push_back(jobId);
	}

	curl_multi_wakeup(m_multi);
}

void TransferThread::Run()
{
	while (m_running)
	{
		std::vector<std::unique_ptr<S3Job>> incoming;
		std::vector<int> cancelRequests;
		TakePending(incoming, cancelRequests);
		AdmitIncoming(incoming);
		ApplyCancellations(cancelRequests);
		StartDueRetries();

		int stillRunning = 0;
		curl_multi_perform(m_multi, &stillRunning);
		ReapCompleted();
		RemoveFinished();

		const long waitMs = ComputeWaitMs();
		curl_multi_poll(m_multi, nullptr, 0, static_cast<int>(waitMs), nullptr);
	}
}

void TransferThread::TakePending(std::vector<std::unique_ptr<S3Job>> &incoming, std::vector<int> &cancelRequests)
{
	std::lock_guard<std::mutex> const lock(m_mutex);
	incoming.swap(m_incoming);
	cancelRequests.swap(m_cancelRequests);
}

void TransferThread::AdmitIncoming(std::vector<std::unique_ptr<S3Job>> &incoming)
{
	for (auto &job : incoming)
	{
		job->BeginAttempt(m_multi);
		m_active.push_back(std::move(job));
	}
}

void TransferThread::ApplyCancellations(const std::vector<int> &cancelRequests)
{
	for (int const jobId : cancelRequests)
	{
		for (auto &job : m_active)
		{
			if (job->Id() == jobId && !job->IsFinished())
			{
				job->Cancel(m_multi);
			}
		}
	}
}

void TransferThread::ReapCompleted()
{
	int remaining = 0;
	CURLMsg *message;

	while ((message = curl_multi_info_read(m_multi, &remaining)) != nullptr)
	{
		if (message->msg != CURLMSG_DONE)
		{
			continue;
		}

		S3Job *job = nullptr;
		curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &job);

		if (job == nullptr)
		{
			continue;
		}

		job->OnAttemptDone(m_multi, message->data.result);
	}
}

void TransferThread::StartDueRetries()
{
	const auto now = std::chrono::steady_clock::now();

	for (auto &job : m_active)
	{
		if (job->IsRetryDue(now))
		{
			job->BeginAttempt(m_multi);
		}
	}
}

long TransferThread::ComputeWaitMs() const
{
	const auto now = std::chrono::steady_clock::now();
	long waitMs = kMaxWaitMs;

	for (const auto &job : m_active)
	{
		if (!job->IsWaitingForRetry())
		{
			continue;
		}

		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(job->RetryAt() - now).count();
		waitMs = std::min(waitMs, std::max(0L, static_cast<long>(remaining)));
	}

	return waitMs;
}

void TransferThread::RemoveFinished()
{
	m_active.erase(
		std::remove_if(
			m_active.begin(), m_active.end(), [](const std::unique_ptr<S3Job> &job) { return job->IsFinished(); }
		),
		m_active.end()
	);
}
