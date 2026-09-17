#pragma once

#include <string>
#include <utility>
#include <vector>

namespace s3
{

using QueryParams = std::vector<std::pair<std::string, std::string>>;

struct Endpoint
{
	std::string scheme;
	std::string host;
};

std::string UriEncode(const std::string &input, bool encodeSlash);
std::string EncodeKeyPath(const std::string &key);
std::string BuildCanonicalQuery(const QueryParams &params);
Endpoint ParseEndpoint(const std::string &endpoint);
std::string TrimSlashes(const std::string &value);

}
