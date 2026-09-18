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

bool ParseErrorBody(const std::string &xml, std::string &code, std::string &message);
ListResult ParseListObjects(const std::string &xml);

}
