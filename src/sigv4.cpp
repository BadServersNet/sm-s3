#include "sigv4.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include "text.h"

namespace s3
{

static const char kAlgorithm[] = "AWS4-HMAC-SHA256";
static const char kService[] = "s3";
static const char kTerminator[] = "aws4_request";

std::string ToHex(const std::string &bytes)
{
	static const char hex[] = "0123456789abcdef";
	std::string out;
	out.reserve(bytes.size() * 2);

	for (unsigned char const c : bytes)
	{
		out.push_back(hex[c >> 4]);
		out.push_back(hex[c & 0x0F]);
	}

	return out;
}

std::string Sha256Hex(const std::string &data)
{
	unsigned char digest[32];
	mbedtls_sha256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest, 0);

	return ToHex(std::string(reinterpret_cast<const char *>(digest), sizeof(digest)));
}

std::string HmacSha256(const std::string &key, const std::string &data)
{
	unsigned char digest[32];
	const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	mbedtls_md_hmac(
		info,
		reinterpret_cast<const unsigned char *>(key.data()),
		key.size(),
		reinterpret_cast<const unsigned char *>(data.data()),
		data.size(),
		digest
	);

	return std::string(reinterpret_cast<const char *>(digest), sizeof(digest));
}

void FormatAmzDate(time_t now, std::string &amzDate, std::string &dateStamp)
{
	struct tm utc;
	gmtime_r(&now, &utc);
	char buffer[32];
	strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
	amzDate = buffer;
	dateStamp = amzDate.substr(0, 8);
}

static HeaderList BuildSignedHeaders(const SigningInput &input, const std::string &amzDate, bool includeAmzHeaders)
{
	HeaderList headers;
	headers.emplace_back("host", input.host);

	if (includeAmzHeaders)
	{
		headers.emplace_back("x-amz-content-sha256", input.payloadHash);
		headers.emplace_back("x-amz-date", amzDate);
	}

	for (const auto &header : input.extraHeaders)
	{
		headers.emplace_back(ToLower(header.first), Trim(header.second));
	}

	std::sort(headers.begin(), headers.end());

	return headers;
}

static std::string JoinHeaderNames(const HeaderList &headers)
{
	std::string out;

	for (const auto &header : headers)
	{
		if (!out.empty())
		{
			out.push_back(';');
		}

		out += header.first;
	}

	return out;
}

static std::string BuildCanonicalRequest(
	const SigningInput &input,
	const std::string &canonicalQuery,
	const HeaderList &headers,
	const std::string &signedHeaderNames
)
{
	std::string canonical;
	canonical += input.method;
	canonical.push_back('\n');
	canonical += input.canonicalUri;
	canonical.push_back('\n');
	canonical += canonicalQuery;
	canonical.push_back('\n');

	for (const auto &header : headers)
	{
		canonical += header.first;
		canonical.push_back(':');
		canonical += header.second;
		canonical.push_back('\n');
	}

	canonical.push_back('\n');
	canonical += signedHeaderNames;
	canonical.push_back('\n');
	canonical += input.payloadHash;

	return canonical;
}

static std::string BuildScope(const std::string &dateStamp, const std::string &region)
{
	return dateStamp + "/" + region + "/" + kService + "/" + kTerminator;
}

static std::string
BuildStringToSign(const std::string &amzDate, const std::string &scope, const std::string &canonicalRequest)
{
	std::string out = kAlgorithm;
	out.push_back('\n');
	out += amzDate;
	out.push_back('\n');
	out += scope;
	out.push_back('\n');
	out += Sha256Hex(canonicalRequest);

	return out;
}

static std::string DeriveSigningKey(const Credentials &credentials, const std::string &dateStamp)
{
	const std::string dateKey = HmacSha256("AWS4" + credentials.secretKey, dateStamp);
	const std::string regionKey = HmacSha256(dateKey, credentials.region);
	const std::string serviceKey = HmacSha256(regionKey, kService);

	return HmacSha256(serviceKey, kTerminator);
}

SigningOutput SignRequest(
	const SigningInput &input, const Credentials &credentials, const std::string &amzDate, const std::string &dateStamp
)
{
	SigningOutput output;
	output.amzDate = amzDate;

	const HeaderList headers = BuildSignedHeaders(input, amzDate, true);
	const std::string signedHeaderNames = JoinHeaderNames(headers);
	const std::string canonicalQuery = BuildCanonicalQuery(input.query);
	const std::string scope = BuildScope(dateStamp, credentials.region);

	output.canonicalRequest = BuildCanonicalRequest(input, canonicalQuery, headers, signedHeaderNames);
	output.stringToSign = BuildStringToSign(amzDate, scope, output.canonicalRequest);

	const std::string signingKey = DeriveSigningKey(credentials, dateStamp);
	output.signature = ToHex(HmacSha256(signingKey, output.stringToSign));

	output.authorization = std::string(kAlgorithm) + " Credential=" + credentials.accessKey + "/" + scope
						   + ", SignedHeaders=" + signedHeaderNames + ", Signature=" + output.signature;

	output.headers.emplace_back("x-amz-content-sha256", input.payloadHash);
	output.headers.emplace_back("x-amz-date", amzDate);
	output.headers.emplace_back("Authorization", output.authorization);

	for (const auto &header : input.extraHeaders)
	{
		output.headers.push_back(header);
	}

	return output;
}

std::string PresignQuery(
	const SigningInput &input,
	const Credentials &credentials,
	const std::string &amzDate,
	const std::string &dateStamp,
	int expiresSeconds
)
{
	const std::string scope = BuildScope(dateStamp, credentials.region);
	const HeaderList headers = BuildSignedHeaders(input, amzDate, false);
	const std::string signedHeaderNames = JoinHeaderNames(headers);

	QueryParams query = input.query;
	query.emplace_back("X-Amz-Algorithm", kAlgorithm);
	query.emplace_back("X-Amz-Credential", credentials.accessKey + "/" + scope);
	query.emplace_back("X-Amz-Date", amzDate);
	query.emplace_back("X-Amz-Expires", std::to_string(expiresSeconds));
	query.emplace_back("X-Amz-SignedHeaders", signedHeaderNames);

	SigningInput presignInput = input;
	presignInput.payloadHash = "UNSIGNED-PAYLOAD";
	const std::string canonicalQuery = BuildCanonicalQuery(query);
	const std::string canonicalRequest =
		BuildCanonicalRequest(presignInput, canonicalQuery, headers, signedHeaderNames);
	const std::string stringToSign = BuildStringToSign(amzDate, scope, canonicalRequest);
	const std::string signingKey = DeriveSigningKey(credentials, dateStamp);
	const std::string signature = ToHex(HmacSha256(signingKey, stringToSign));

	return canonicalQuery + "&X-Amz-Signature=" + signature;
}

}
