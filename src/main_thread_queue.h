#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

enum class S3Status
{
	Ok = 0,
	HttpError,
	NetworkError,
	Timeout,
	IoError,
	Cancelled,
};

struct TransferEvent
{
	enum class Kind
	{
		Progress,
		Complete,
	};

	Kind kind = Kind::Complete;
	int jobId = 0;
	long long transferred = 0;
	long long total = 0;
	S3Status status = S3Status::Ok;
	long httpStatus = 0;
	long long contentLength = -1;
	std::string error;
	std::map<std::string, std::string> headers;
	std::string body;
};

class MainThreadQueue
{
public:
	void Post(TransferEvent &&event);
	std::vector<TransferEvent> Drain();
	void SetEnabled(bool enabled);

	bool HasEvents() const { return m_hasEvents; }

private:
	std::mutex m_mutex;
	std::vector<TransferEvent> m_events;
	std::atomic<bool> m_hasEvents { false };
	std::atomic<bool> m_enabled { true };
};
