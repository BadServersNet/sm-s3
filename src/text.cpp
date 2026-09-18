#include "text.h"

#include <algorithm>
#include <cctype>

namespace s3
{

static char LowerChar(unsigned char c)
{
	return static_cast<char>(std::tolower(c));
}

std::string ToLower(const std::string &value)
{
	std::string out = value;
	std::transform(out.begin(), out.end(), out.begin(), LowerChar);

	return out;
}

std::string Trim(const std::string &value)
{
	const size_t start = value.find_first_not_of(" \t\r\n");

	if (start == std::string::npos)
	{
		return "";
	}

	const size_t end = value.find_last_not_of(" \t\r\n");

	return value.substr(start, end - start + 1);
}

}
