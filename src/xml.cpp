#include "xml.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace s3
{

std::string XmlUnescape(const std::string &value)
{
	static const struct
	{
		const char *entity;
		char replacement;
	} entities[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
	};

	std::string out;
	out.reserve(value.size());
	size_t i = 0;

	while (i < value.size())
	{
		if (value[i] != '&')
		{
			out.push_back(value[i]);
			i++;
			continue;
		}

		bool replaced = false;

		for (const auto &entity : entities)
		{
			const size_t length = strlen(entity.entity);

			if (value.compare(i, length, entity.entity) == 0)
			{
				out.push_back(entity.replacement);
				i += length;
				replaced = true;
				break;
			}
		}

		if (!replaced)
		{
			out.push_back(value[i]);
			i++;
		}
	}

	return out;
}

bool XmlFindElement(const std::string &xml, const std::string &tag, size_t from, size_t &start, size_t &end)
{
	const std::string open = "<" + tag;
	const std::string close = "</" + tag + ">";
	size_t openPos = xml.find(open, from);

	while (openPos != std::string::npos)
	{
		const size_t afterName = openPos + open.size();
		const char next = afterName < xml.size() ? xml[afterName] : '\0';

		if (next == '>' || next == ' ' || next == '/')
		{
			break;
		}

		openPos = xml.find(open, afterName);
	}

	if (openPos == std::string::npos)
	{
		return false;
	}

	const size_t contentStart = xml.find('>', openPos);

	if (contentStart == std::string::npos)
	{
		return false;
	}

	if (xml[contentStart - 1] == '/')
	{
		start = contentStart + 1;
		end = contentStart + 1;

		return true;
	}

	const size_t closePos = xml.find(close, contentStart);

	if (closePos == std::string::npos)
	{
		return false;
	}

	start = contentStart + 1;
	end = closePos;

	return true;
}

std::string XmlText(const std::string &xml, const std::string &tag)
{
	size_t start;
	size_t end;

	if (!XmlFindElement(xml, tag, 0, start, end))
	{
		return "";
	}

	return XmlUnescape(xml.substr(start, end - start));
}

bool ParseErrorBody(const std::string &xml, std::string &code, std::string &message)
{
	code = XmlText(xml, "Code");
	message = XmlText(xml, "Message");

	return !code.empty();
}

long long ParseIso8601(const std::string &value)
{
	struct tm parts;
	memset(&parts, 0, sizeof(parts));
	const int fields = sscanf(
		value.c_str(),
		"%4d-%2d-%2dT%2d:%2d:%2d",
		&parts.tm_year,
		&parts.tm_mon,
		&parts.tm_mday,
		&parts.tm_hour,
		&parts.tm_min,
		&parts.tm_sec
	);

	if (fields != 6)
	{
		return 0;
	}

	parts.tm_year -= 1900;
	parts.tm_mon -= 1;

	return static_cast<long long>(timegm(&parts));
}

static ListedObject ParseContents(const std::string &contents)
{
	ListedObject object;
	object.key = XmlText(contents, "Key");
	object.size = atoll(XmlText(contents, "Size").c_str());
	object.lastModified = ParseIso8601(XmlText(contents, "LastModified"));
	object.etag = XmlText(contents, "ETag");

	return object;
}

ListResult ParseListObjects(const std::string &xml)
{
	ListResult result;
	result.truncated = XmlText(xml, "IsTruncated") == "true";
	result.nextToken = XmlText(xml, "NextContinuationToken");

	size_t from = 0;
	size_t start;
	size_t end;

	while (XmlFindElement(xml, "Contents", from, start, end))
	{
		result.objects.push_back(ParseContents(xml.substr(start, end - start)));
		from = end;
	}

	return result;
}

}
