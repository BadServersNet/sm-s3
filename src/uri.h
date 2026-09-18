#pragma once

#include <string>

namespace s3
{

struct Endpoint
{
	std::string scheme;
	std::string host;
};

std::string UriEncode(const std::string &input, bool encodeSlash);
std::string EncodeKeyPath(const std::string &key);
Endpoint ParseEndpoint(const std::string &endpoint);
std::string TrimSlashes(const std::string &value);

}
