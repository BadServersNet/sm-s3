#include "uri.h"

namespace s3
{

static bool IsUnreserved(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return true;
	}

	if (c >= 'a' && c <= 'z')
	{
		return true;
	}

	if (c >= '0' && c <= '9')
	{
		return true;
	}

	return c == '-' || c == '_' || c == '.' || c == '~';
}

std::string UriEncode(const std::string &input, bool encodeSlash)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(input.size() * 3);

	for (unsigned char const c : input)
	{
		if (IsUnreserved(c) || (c == '/' && !encodeSlash))
		{
			out.push_back(static_cast<char>(c));
			continue;
		}

		out.push_back('%');
		out.push_back(hex[c >> 4]);
		out.push_back(hex[c & 0x0F]);
	}

	return out;
}

std::string EncodeKeyPath(const std::string &key)
{
	return UriEncode(key, false);
}

Endpoint ParseEndpoint(const std::string &endpoint)
{
	Endpoint result;
	result.scheme = "https";
	std::string rest = endpoint;

	const size_t schemePos = rest.find("://");

	if (schemePos != std::string::npos)
	{
		result.scheme = rest.substr(0, schemePos);
		rest = rest.substr(schemePos + 3);
	}

	result.host = TrimSlashes(rest);

	return result;
}

std::string TrimSlashes(const std::string &value)
{
	size_t start = 0;
	size_t end = value.size();

	while (start < end && value[start] == '/')
	{
		start++;
	}

	while (end > start && value[end - 1] == '/')
	{
		end--;
	}

	return value.substr(start, end - start);
}

}
