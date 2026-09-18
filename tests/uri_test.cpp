#include <cstdio>
#include <string>

#include "uri.h"

static int failures = 0;

static void Expect(const char *name, const std::string &actual, const std::string &expected)
{
	if (actual == expected)
	{
		printf("ok   %s\n", name);

		return;
	}

	failures++;
	printf("FAIL %s\n  expected: %s\n  actual:   %s\n", name, expected.c_str(), actual.c_str());
}

static void TestUri()
{
	Expect("encode key path", s3::EncodeKeyPath("runs/kz_map/1 2.replay"), "runs/kz_map/1%202.replay");
	Expect("encode key slash", s3::UriEncode("a/b", true), "a%2Fb");
	Expect(
		"endpoint host",
		s3::ParseEndpoint("https://acct.r2.cloudflarestorage.com/").host,
		"acct.r2.cloudflarestorage.com"
	);
	Expect("endpoint scheme", s3::ParseEndpoint("http://127.0.0.1:9000").scheme, "http");
}

int main()
{
	TestUri();

	if (failures > 0)
	{
		printf("%d failure(s)\n", failures);

		return 1;
	}

	printf("all passed\n");

	return 0;
}
