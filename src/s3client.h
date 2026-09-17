#pragma once

#include <string>

#include "uri.h"

namespace s3
{

struct ClientConfig
{
	Endpoint endpoint;
	std::string bucket;
	std::string region;
	std::string accessKey;
	std::string secretKey;
	std::string publicUrl;
	bool pathStyle = true;
	int connectTimeout = 10;
	int timeout = 60;
	int maxRetries = 3;
	long maxSendSpeed = 0;
	long maxRecvSpeed = 0;
};

std::string BuildHost(const ClientConfig &config);
std::string BuildObjectUri(const ClientConfig &config, const std::string &key);
std::string BuildBucketUri(const ClientConfig &config);

}
