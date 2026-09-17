#include "s3client.h"

namespace s3
{

std::string BuildHost(const ClientConfig &config)
{
	if (config.pathStyle)
	{
		return config.endpoint.host;
	}
	return config.bucket + "." + config.endpoint.host;
}

std::string BuildBucketUri(const ClientConfig &config)
{
	if (config.pathStyle)
	{
		return "/" + config.bucket + "/";
	}
	return "/";
}

std::string BuildObjectUri(const ClientConfig &config, const std::string &key)
{
	return BuildBucketUri(config) + EncodeKeyPath(key);
}

}
