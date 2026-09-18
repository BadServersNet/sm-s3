#include "s3job.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <random>

#include <unistd.h>

#include "text.h"
#include "xml.h"

static const size_t kMaxErrorBody = size_t { 64 } * 1024;
static const size_t kMaxListBody = size_t { 4 } * 1024 * 1024;
static const long kBackoffBaseMs = 1000;
static const long kBackoffCapMs = 30000;

static const char *MethodName(S3Op op)
{
	switch (op)
	{
		case S3Op::Head:
			return "HEAD";
		case S3Op::Get:
			return "GET";
		case S3Op::Put:
			return "PUT";
		case S3Op::Delete:
			return "DELETE";
		case S3Op::Copy:
			return "PUT";
		case S3Op::List:
			return "GET";
	}

	return "GET";
}

static bool IsSuccess(long httpStatus)
{
	return httpStatus >= 200 && httpStatus < 300;
}

static long long FileSizeOf(const std::string &path)
{
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);

	if (error)
	{
		return -1;
	}

	return static_cast<long long>(size);
}

static void EnsureParentDirectory(const std::string &path)
{
	const std::filesystem::path parent = std::filesystem::path(path).parent_path();
	std::error_code error;
	std::filesystem::create_directories(parent, error);
}

S3Job::S3Job(JobSpec spec, MainThreadQueue *queue) : m_spec(std::move(spec)), m_queue(queue)
{
	m_errorBuffer[0] = '\0';
}

S3Job::~S3Job()
{
	CloseFiles();

	if (m_headerList != nullptr)
	{
		curl_slist_free_all(m_headerList);
	}

	if (m_easy != nullptr)
	{
		curl_easy_cleanup(m_easy);
	}
}

bool S3Job::IsRetryDue(std::chrono::steady_clock::time_point now) const
{
	return m_waitingForRetry && now >= m_retryAt;
}

void S3Job::ResetAttemptState()
{
	m_responseBody.clear();
	m_responseHeaders.clear();
	m_httpStatus = 0;
	m_errorBuffer[0] = '\0';
	m_restartedFullBody = false;
	m_lastProgressValue = -1;
	m_lastProgressPost = std::chrono::steady_clock::time_point();
	m_waitingForRetry = false;
}

bool S3Job::BeginAttempt(CURLM *multi)
{
	m_attempt++;
	ResetAttemptState();

	if (m_cancelRequested)
	{
		Finish(S3Status::Cancelled, "cancelled");

		return false;
	}

	m_easy = curl_easy_init();

	if (m_easy == nullptr)
	{
		Finish(S3Status::NetworkError, "curl_easy_init failed");

		return false;
	}

	if (!OpenFiles())
	{
		const std::string error = std::string(strerror(errno)) + ": " + m_spec.filePath;
		Finish(S3Status::IoError, error);

		return false;
	}

	BuildRequest();
	ApplyCommonOptions();
	ApplyMethodOptions();

	const CURLMcode added = curl_multi_add_handle(multi, m_easy);

	if (added != CURLM_OK)
	{
		Finish(S3Status::NetworkError, curl_multi_strerror(added));

		return false;
	}

	m_attached = true;

	return true;
}

bool S3Job::OpenFiles()
{
	if (m_spec.op == S3Op::Put)
	{
		m_file = fopen(m_spec.filePath.c_str(), "rb");

		if (m_file == nullptr)
		{
			return false;
		}

		const long long size = FileSizeOf(m_spec.filePath);
		m_uploadSize = size < 0 ? 0 : static_cast<curl_off_t>(size);

		return true;
	}

	if (m_spec.op != S3Op::Get)
	{
		return true;
	}

	m_partPath = m_spec.filePath + ".part";
	EnsureParentDirectory(m_partPath);
	const long long existing = m_spec.resume ? FileSizeOf(m_partPath) : -1;

	if (existing > 0)
	{
		m_file = fopen(m_partPath.c_str(), "ab");
		m_resumeOffset = static_cast<curl_off_t>(existing);
	}
	else
	{
		m_file = fopen(m_partPath.c_str(), "wb");
		m_resumeOffset = 0;
	}

	return m_file != nullptr;
}

void S3Job::CloseFiles()
{
	if (m_file != nullptr)
	{
		fclose(m_file);
		m_file = nullptr;
	}
}

void S3Job::ResetHeaderList()
{
	if (m_headerList == nullptr)
	{
		return;
	}

	curl_slist_free_all(m_headerList);
	m_headerList = nullptr;
}

void S3Job::AppendHeader(const std::string &line)
{
	m_headerList = curl_slist_append(m_headerList, line.c_str());
}

void S3Job::AppendRangeHeader()
{
	if (m_spec.op != S3Op::Get || m_resumeOffset <= 0)
	{
		return;
	}

	const long long offset = static_cast<long long>(m_resumeOffset);
	const std::string range = "Range: bytes=" + std::to_string(offset) + "-";
	AppendHeader(range);
}

void S3Job::BuildPublicRequest()
{
	const std::string baseUrl = s3::TrimSlashes(m_spec.config.publicUrl);
	const std::string url = baseUrl + "/" + s3::EncodeKeyPath(m_spec.key);
	curl_easy_setopt(m_easy, CURLOPT_URL, url.c_str());

	AppendRangeHeader();
	curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_headerList);
}

std::string S3Job::BuildListQuery() const
{
	std::string query = "list-type=2&max-keys=" + std::to_string(m_spec.maxKeys);

	if (!m_spec.prefix.empty())
	{
		query += "&prefix=" + s3::UriEncode(m_spec.prefix, true);
	}

	if (!m_spec.continuationToken.empty())
	{
		query += "&continuation-token=" + s3::UriEncode(m_spec.continuationToken, true);
	}

	return query;
}

std::string S3Job::BuildSignedUrl() const
{
	const s3::ClientConfig &config = m_spec.config;
	const std::string origin = config.endpoint.scheme + "://" + s3::BuildHost(config);

	if (m_spec.op == S3Op::List)
	{
		return origin + s3::BuildBucketUri(config) + "?" + BuildListQuery();
	}

	return origin + s3::BuildObjectUri(config, m_spec.key);
}

void S3Job::ApplySigning()
{
	const s3::ClientConfig &config = m_spec.config;
	const std::string provider = "aws:amz:" + config.region + ":s3";
	curl_easy_setopt(m_easy, CURLOPT_AWS_SIGV4, provider.c_str());
	curl_easy_setopt(m_easy, CURLOPT_USERNAME, config.accessKey.c_str());
	curl_easy_setopt(m_easy, CURLOPT_PASSWORD, config.secretKey.c_str());
}

void S3Job::BuildRequest()
{
	ResetHeaderList();

	const s3::ClientConfig &config = m_spec.config;
	const bool usePublicUrl = m_spec.op == S3Op::Get && !config.publicUrl.empty();

	if (usePublicUrl)
	{
		BuildPublicRequest();
		return;
	}

	const std::string url = BuildSignedUrl();
	curl_easy_setopt(m_easy, CURLOPT_URL, url.c_str());
	ApplySigning();

	if (m_spec.op == S3Op::Copy)
	{
		const std::string source = "/" + config.bucket + "/" + s3::EncodeKeyPath(m_spec.sourceKey);
		AppendHeader("x-amz-copy-source: " + source);
	}

	if (m_spec.op == S3Op::Put)
	{
		AppendHeader("Content-Type: " + m_spec.contentType);
	}

	AppendRangeHeader();
	curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_headerList);
}

void S3Job::ApplyCommonOptions()
{
	const s3::ClientConfig &config = m_spec.config;
	curl_easy_setopt(m_easy, CURLOPT_PRIVATE, this);
	curl_easy_setopt(m_easy, CURLOPT_ERRORBUFFER, m_errorBuffer);
	curl_easy_setopt(m_easy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(m_easy, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(m_easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(m_easy, CURLOPT_CONNECTTIMEOUT, static_cast<long>(config.connectTimeout));
	curl_easy_setopt(m_easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(m_easy, CURLOPT_LOW_SPEED_TIME, static_cast<long>(config.timeout));
	curl_easy_setopt(m_easy, CURLOPT_MAX_SEND_SPEED_LARGE, static_cast<curl_off_t>(config.maxSendSpeed));
	curl_easy_setopt(m_easy, CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(config.maxRecvSpeed));
	curl_easy_setopt(m_easy, CURLOPT_HEADERFUNCTION, HeaderCallback);
	curl_easy_setopt(m_easy, CURLOPT_HEADERDATA, this);
	curl_easy_setopt(m_easy, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(m_easy, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(m_easy, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
	curl_easy_setopt(m_easy, CURLOPT_XFERINFODATA, this);
	curl_easy_setopt(m_easy, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(m_easy, CURLOPT_USERAGENT, "sm-s3/" SM_S3_VERSION);

	if (!m_spec.caBundle.empty())
	{
		curl_easy_setopt(m_easy, CURLOPT_CAINFO, m_spec.caBundle.c_str());
	}

	if (m_spec.verbose)
	{
		curl_easy_setopt(m_easy, CURLOPT_VERBOSE, 1L);
	}
}

void S3Job::ApplyMethodOptions()
{
	switch (m_spec.op)
	{
		case S3Op::Head:
		{
			curl_easy_setopt(m_easy, CURLOPT_NOBODY, 1L);
			break;
		}

		case S3Op::Put:
		{
			curl_easy_setopt(m_easy, CURLOPT_UPLOAD, 1L);
			curl_easy_setopt(m_easy, CURLOPT_READFUNCTION, ReadCallback);
			curl_easy_setopt(m_easy, CURLOPT_READDATA, this);
			curl_easy_setopt(m_easy, CURLOPT_INFILESIZE_LARGE, m_uploadSize);
			break;
		}

		case S3Op::Copy:
		{
			curl_easy_setopt(m_easy, CURLOPT_CUSTOMREQUEST, "PUT");
			break;
		}

		case S3Op::Delete:
		{
			curl_easy_setopt(m_easy, CURLOPT_CUSTOMREQUEST, "DELETE");
			break;
		}

		case S3Op::Get:
		case S3Op::List:
		{
			curl_easy_setopt(m_easy, CURLOPT_HTTPGET, 1L);
			break;
		}
	}
}

size_t S3Job::HeaderCallback(char *data, size_t size, size_t count, void *userdata)
{
	S3Job *job = static_cast<S3Job *>(userdata);
	const size_t total = size * count;
	const std::string line(data, total);
	const size_t colon = line.find(':');

	if (colon == std::string::npos)
	{
		return total;
	}

	const std::string name = s3::ToLower(s3::Trim(line.substr(0, colon)));
	const std::string value = s3::Trim(line.substr(colon + 1));
	job->m_responseHeaders[name] = value;

	return total;
}

size_t S3Job::WriteCallback(char *data, size_t size, size_t count, void *userdata)
{
	S3Job *job = static_cast<S3Job *>(userdata);
	const size_t total = size * count;
	long status = 0;
	curl_easy_getinfo(job->m_easy, CURLINFO_RESPONSE_CODE, &status);

	const bool bodyToFile = job->m_spec.op == S3Op::Get && IsSuccess(status);

	if (!bodyToFile)
	{
		const size_t cap = job->m_spec.op == S3Op::List ? kMaxListBody : kMaxErrorBody;

		if (job->m_responseBody.size() < cap)
		{
			job->m_responseBody.append(data, std::min(total, cap - job->m_responseBody.size()));
		}

		return total;
	}

	const bool serverIgnoredRange = status == 200 && job->m_resumeOffset > 0 && !job->m_restartedFullBody;

	if (serverIgnoredRange)
	{
		job->m_restartedFullBody = true;
		job->m_resumeOffset = 0;
		FILE *reopened = freopen(job->m_partPath.c_str(), "wb", job->m_file);

		if (reopened == nullptr)
		{
			job->m_file = nullptr;

			return 0;
		}

		job->m_file = reopened;
	}

	const size_t written = fwrite(data, 1, total, job->m_file);

	return written;
}

size_t S3Job::ReadCallback(char *buffer, size_t size, size_t count, void *userdata)
{
	S3Job *job = static_cast<S3Job *>(userdata);

	if (job->m_file == nullptr)
	{
		return CURL_READFUNC_ABORT;
	}

	return fread(buffer, 1, size * count, job->m_file);
}

int S3Job::ProgressCallback(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	S3Job *job = static_cast<S3Job *>(userdata);

	if (job->m_cancelRequested)
	{
		return 1;
	}

	if (job->m_spec.op == S3Op::Put)
	{
		job->PostProgress(static_cast<long long>(ulnow), static_cast<long long>(ultotal));

		return 0;
	}

	if (job->m_spec.op == S3Op::Get)
	{
		const long long offset = static_cast<long long>(job->m_resumeOffset);
		const long long total = dltotal > 0 ? static_cast<long long>(dltotal) + offset : 0;
		job->PostProgress(static_cast<long long>(dlnow) + offset, total);
	}

	return 0;
}

void S3Job::PostProgress(long long transferred, long long total)
{
	if (transferred <= 0 && total <= 0)
	{
		return;
	}

	if (transferred == m_lastProgressValue)
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProgressPost).count();
	const bool complete = total > 0 && transferred >= total;
	const bool percentStep =
		total > 0 && m_lastProgressValue >= 0 && (transferred - m_lastProgressValue) * 100 >= total;

	if (!complete && !percentStep && elapsed < 100)
	{
		return;
	}

	m_lastProgressPost = now;
	m_lastProgressValue = transferred;

	TransferEvent event;
	event.kind = TransferEvent::Kind::Progress;
	event.jobId = m_spec.id;
	event.transferred = transferred;
	event.total = total;
	m_queue->Post(std::move(event));
}

void S3Job::ReleaseCurl(CURLM *multi)
{
	if (m_easy == nullptr)
	{
		return;
	}

	if (m_attached)
	{
		curl_multi_remove_handle(multi, m_easy);
		m_attached = false;
	}

	curl_easy_cleanup(m_easy);
	m_easy = nullptr;
}

bool S3Job::ShouldRetryStatus(long httpStatus) const
{
	return httpStatus == 429 || httpStatus >= 500;
}

void S3Job::ScheduleRetry()
{
	static thread_local std::mt19937 generator { std::random_device {}() };
	const long exponent = std::min(m_attempt - 1, 10);
	const long base = std::min(kBackoffCapMs, kBackoffBaseMs * (1L << exponent));
	std::uniform_int_distribution<long> jitter(-base / 4, base / 4);
	const long delay = base + jitter(generator);
	m_retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
	m_waitingForRetry = true;
}

std::string S3Job::DescribeHttpError() const
{
	std::string code;
	std::string message;

	if (s3::ParseErrorBody(m_responseBody, code, message))
	{
		if (message.empty())
		{
			return code;
		}

		return code + ": " + message;
	}

	return "HTTP " + std::to_string(m_httpStatus);
}

bool S3Job::FinalizeDownload(std::string &error)
{
	if (rename(m_partPath.c_str(), m_spec.filePath.c_str()) != 0)
	{
		error = std::string("rename failed: ") + strerror(errno);

		return false;
	}

	return true;
}

void S3Job::HandleTransportFailure(CURLcode code, const std::string &curlError, bool canRetry)
{
	if (code == CURLE_ABORTED_BY_CALLBACK)
	{
		Finish(S3Status::Cancelled, "cancelled");
		return;
	}

	if (code == CURLE_WRITE_ERROR || code == CURLE_READ_ERROR)
	{
		Finish(S3Status::IoError, curlError);
		return;
	}

	if (canRetry)
	{
		ScheduleRetry();
		return;
	}

	const bool timedOut = code == CURLE_OPERATION_TIMEDOUT;
	const S3Status status = timedOut ? S3Status::Timeout : S3Status::NetworkError;
	Finish(status, curlError);
}

void S3Job::HandleHttpFailure(bool canRetry)
{
	if (ShouldRetryStatus(m_httpStatus) && canRetry)
	{
		ScheduleRetry();
		return;
	}

	const bool clientError = m_httpStatus >= 400 && m_httpStatus < 500;

	if (m_spec.op == S3Op::Get && clientError)
	{
		unlink(m_partPath.c_str());
	}

	Finish(S3Status::HttpError, DescribeHttpError());
}

void S3Job::HandleHttpSuccess()
{
	if (m_spec.op != S3Op::Get)
	{
		Finish(S3Status::Ok, "");
		return;
	}

	std::string error;
	const bool finalized = FinalizeDownload(error);

	if (!finalized)
	{
		Finish(S3Status::IoError, error);
		return;
	}

	Finish(S3Status::Ok, "");
}

void S3Job::OnAttemptDone(CURLM *multi, CURLcode code)
{
	curl_easy_getinfo(m_easy, CURLINFO_RESPONSE_CODE, &m_httpStatus);
	const bool hasErrorText = m_errorBuffer[0] != '\0';
	const std::string curlError = hasErrorText ? m_errorBuffer : curl_easy_strerror(code);
	CloseFiles();
	ReleaseCurl(multi);

	if (m_cancelRequested)
	{
		Finish(S3Status::Cancelled, "cancelled");
		return;
	}

	const bool canRetry = m_attempt <= m_spec.config.maxRetries;

	if (code != CURLE_OK)
	{
		HandleTransportFailure(code, curlError, canRetry);
		return;
	}

	if (IsSuccess(m_httpStatus))
	{
		HandleHttpSuccess();
		return;
	}

	HandleHttpFailure(canRetry);
}

void S3Job::Cancel(CURLM *multi)
{
	m_cancelRequested = true;
	CloseFiles();
	ReleaseCurl(multi);
	Finish(S3Status::Cancelled, "cancelled");
}

void S3Job::Finish(S3Status status, const std::string &error)
{
	if (m_finished)
	{
		return;
	}

	m_finished = true;
	m_waitingForRetry = false;

	TransferEvent event;
	event.kind = TransferEvent::Kind::Complete;
	event.jobId = m_spec.id;
	event.status = status;
	event.httpStatus = m_httpStatus;
	event.error = error;
	event.headers = m_responseHeaders;

	if (m_spec.op == S3Op::List)
	{
		event.body = m_responseBody;
	}

	const auto lengthHeader = m_responseHeaders.find("content-length");

	if (lengthHeader != m_responseHeaders.end())
	{
		event.contentLength = atoll(lengthHeader->second.c_str());
	}

	m_queue->Post(std::move(event));
}
