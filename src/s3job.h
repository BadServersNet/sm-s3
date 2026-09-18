#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <string>

#include <curl/curl.h>

#include "main_thread_queue.h"
#include "s3client.h"
#include "sigv4.h"

enum class S3Op
{
	Head,
	Get,
	Put,
	Delete,
	Copy,
	List,
};

struct JobSpec
{
	int id = 0;
	S3Op op = S3Op::Head;
	s3::ClientConfig config;
	std::string key;
	std::string sourceKey;
	std::string prefix;
	std::string filePath;
	std::string contentType;
	bool resume = true;
	int maxKeys = 1000;
	std::string continuationToken;
	std::string caBundle;
	bool verbose = false;
};

class S3Job
{
public:
	S3Job(JobSpec spec, MainThreadQueue *queue);
	~S3Job();

	int Id() const { return m_spec.id; }

	CURL *Easy() const { return m_easy; }

	bool IsFinished() const { return m_finished; }

	bool IsWaitingForRetry() const { return m_waitingForRetry; }

	bool IsRetryDue(std::chrono::steady_clock::time_point now) const;

	std::chrono::steady_clock::time_point RetryAt() const { return m_retryAt; }

	bool BeginAttempt(CURLM *multi);
	void OnAttemptDone(CURLM *multi, CURLcode code);
	void Cancel(CURLM *multi);

private:
	bool OpenFiles();
	void CloseFiles();
	void BuildRequest();
	void BuildPublicRequest();
	s3::SigningInput BuildSigningInput() const;
	s3::QueryParams BuildListQuery() const;
	void ResetHeaderList();
	void AppendHeader(const std::string &line);
	void AppendRangeHeader();
	void HandleTransportFailure(CURLcode code, const std::string &curlError, bool canRetry);
	void HandleHttpFailure(bool canRetry);
	void HandleHttpSuccess();
	void ApplyCommonOptions();
	void ApplyMethodOptions();
	bool ShouldRetryStatus(long httpStatus) const;
	void ScheduleRetry();
	void Finish(S3Status status, const std::string &error);
	bool FinalizeDownload(std::string &error);
	std::string DescribeHttpError() const;
	void PostProgress(long long transferred, long long total);
	void ResetAttemptState();
	void ReleaseCurl(CURLM *multi);

	static size_t WriteCallback(char *data, size_t size, size_t count, void *userdata);
	static size_t ReadCallback(char *buffer, size_t size, size_t count, void *userdata);
	static size_t HeaderCallback(char *data, size_t size, size_t count, void *userdata);
	static int
	ProgressCallback(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

	JobSpec m_spec;
	MainThreadQueue *m_queue;
	CURL *m_easy = nullptr;
	curl_slist *m_headerList = nullptr;
	bool m_attached = false;
	FILE *m_file = nullptr;
	std::string m_partPath;
	curl_off_t m_resumeOffset = 0;
	curl_off_t m_uploadSize = 0;
	bool m_restartedFullBody = false;
	std::string m_responseBody;
	std::map<std::string, std::string> m_responseHeaders;
	char m_errorBuffer[CURL_ERROR_SIZE];
	long m_httpStatus = 0;
	int m_attempt = 0;
	bool m_waitingForRetry = false;
	std::chrono::steady_clock::time_point m_retryAt;
	bool m_finished = false;
	std::atomic<bool> m_cancelRequested { false };
	std::chrono::steady_clock::time_point m_lastProgressPost;
	long long m_lastProgressValue = -1;
};
