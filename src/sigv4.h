#pragma once

#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "uri.h"

namespace s3
{

using HeaderList = std::vector<std::pair<std::string, std::string>>;

struct Credentials
{
	std::string accessKey;
	std::string secretKey;
	std::string region;
};

struct SigningInput
{
	std::string method;
	std::string host;
	std::string canonicalUri;
	QueryParams query;
	HeaderList extraHeaders;
	std::string payloadHash = "UNSIGNED-PAYLOAD";
};

struct SigningOutput
{
	std::string amzDate;
	std::string canonicalRequest;
	std::string stringToSign;
	std::string signature;
	std::string authorization;
	HeaderList headers;
};

std::string Sha256Hex(const std::string &data);
std::string HmacSha256(const std::string &key, const std::string &data);
std::string ToHex(const std::string &bytes);
void FormatAmzDate(time_t now, std::string &amzDate, std::string &dateStamp);

SigningOutput SignRequest(const SigningInput &input, const Credentials &credentials, const std::string &amzDate, const std::string &dateStamp);
std::string PresignQuery(const SigningInput &input, const Credentials &credentials, const std::string &amzDate, const std::string &dateStamp, int expiresSeconds);

}
