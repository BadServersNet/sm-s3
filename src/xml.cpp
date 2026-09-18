#include "xml.h"

#include <cstdlib>
#include <ctime>

#include <pugixml.hpp>

namespace s3
{

static long long ParseIso8601(const char *value)
{
	struct tm parts = {};
	const char *parsedEnd = strptime(value, "%Y-%m-%dT%H:%M:%S", &parts);

	if (parsedEnd == nullptr)
	{
		return 0;
	}

	return static_cast<long long>(timegm(&parts));
}

static ListedObject ParseContents(const pugi::xml_node &contents)
{
	ListedObject object;
	object.key = contents.child_value("Key");
	object.size = atoll(contents.child_value("Size"));
	object.lastModified = ParseIso8601(contents.child_value("LastModified"));
	object.etag = contents.child_value("ETag");

	return object;
}

bool ParseErrorBody(const std::string &xml, std::string &code, std::string &message)
{
	pugi::xml_document document;
	document.load_buffer(xml.data(), xml.size());

	const pugi::xml_node error = document.child("Error");
	code = error.child_value("Code");
	message = error.child_value("Message");

	return !code.empty();
}

ListResult ParseListObjects(const std::string &xml)
{
	pugi::xml_document document;
	document.load_buffer(xml.data(), xml.size());

	const pugi::xml_node root = document.child("ListBucketResult");
	const std::string truncated = root.child_value("IsTruncated");

	ListResult result;
	result.truncated = truncated == "true";
	result.nextToken = root.child_value("NextContinuationToken");

	for (const pugi::xml_node &contents : root.children("Contents"))
	{
		result.objects.push_back(ParseContents(contents));
	}

	return result;
}

}
