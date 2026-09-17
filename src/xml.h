#pragma once

#include <string>
#include <vector>

namespace s3
{

struct ListedObject
{
	std::string key;
	long long size = 0;
	long long lastModified = 0;
	std::string etag;
};

struct ListResult
{
	std::vector<ListedObject> objects;
	std::string nextToken;
	bool truncated = false;
};

std::string XmlUnescape(const std::string &value);
bool XmlFindElement(const std::string &xml, const std::string &tag, size_t from, size_t &start, size_t &end);
std::string XmlText(const std::string &xml, const std::string &tag);
bool ParseErrorBody(const std::string &xml, std::string &code, std::string &message);
ListResult ParseListObjects(const std::string &xml);
long long ParseIso8601(const std::string &value);

}
