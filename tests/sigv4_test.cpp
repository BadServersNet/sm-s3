#include <cstdio>
#include <string>

#include "sigv4.h"
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

static s3::Credentials ExampleCredentials()
{
	s3::Credentials credentials;
	credentials.accessKey = "AKIAIOSFODNN7EXAMPLE";
	credentials.secretKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
	credentials.region = "us-east-1";
	return credentials;
}

static void TestHashes()
{
	Expect("sha256 empty", s3::Sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	Expect("sha256 abc", s3::Sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	Expect("hmac rfc4231 case 2", s3::ToHex(s3::HmacSha256("Jefe", "what do ya want for nothing?")),
		"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

static void TestUri()
{
	Expect("encode key path", s3::EncodeKeyPath("runs/kz_map/1 2.replay"), "runs/kz_map/1%202.replay");
	Expect("encode key slash", s3::UriEncode("a/b", true), "a%2Fb");
	s3::QueryParams params = { { "prefix", "runs/" }, { "list-type", "2" } };
	Expect("canonical query", s3::BuildCanonicalQuery(params), "list-type=2&prefix=runs%2F");
	Expect("endpoint host", s3::ParseEndpoint("https://acct.r2.cloudflarestorage.com/").host, "acct.r2.cloudflarestorage.com");
	Expect("endpoint scheme", s3::ParseEndpoint("http://127.0.0.1:9000").scheme, "http");
}

static void TestHeaderSignature()
{
	s3::SigningInput input;
	input.method = "GET";
	input.host = "examplebucket.s3.amazonaws.com";
	input.canonicalUri = "/test.txt";
	input.extraHeaders.emplace_back("Range", "bytes=0-9");
	input.payloadHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

	const s3::SigningOutput output = s3::SignRequest(input, ExampleCredentials(), "20130524T000000Z", "20130524");
	Expect("aws get object canonical request hash", s3::Sha256Hex(output.canonicalRequest),
		"7344ae5b7ee6c3e7e6b0fe0640412a37625d1fbfff95c48bbb2dc43964946972");
	Expect("aws get object signature", output.signature,
		"f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41");
	Expect("aws get object authorization", output.authorization,
		"AWS4-HMAC-SHA256 Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request, "
		"SignedHeaders=host;range;x-amz-content-sha256;x-amz-date, "
		"Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41");
}

static void TestPresign()
{
	s3::SigningInput input;
	input.method = "GET";
	input.host = "examplebucket.s3.amazonaws.com";
	input.canonicalUri = "/test.txt";

	const std::string query = s3::PresignQuery(input, ExampleCredentials(), "20130524T000000Z", "20130524", 86400);
	Expect("aws presigned query", query,
		"X-Amz-Algorithm=AWS4-HMAC-SHA256"
		"&X-Amz-Credential=AKIAIOSFODNN7EXAMPLE%2F20130524%2Fus-east-1%2Fs3%2Faws4_request"
		"&X-Amz-Date=20130524T000000Z&X-Amz-Expires=86400&X-Amz-SignedHeaders=host"
		"&X-Amz-Signature=aeeed9bbccd4d02ee5c0109b86d86835f995330da4c265957d157751f604d404");
}

static void TestAmzDate()
{
	std::string amzDate;
	std::string dateStamp;
	s3::FormatAmzDate(1369353600, amzDate, dateStamp);
	Expect("amz date", amzDate, "20130524T000000Z");
	Expect("date stamp", dateStamp, "20130524");
}

int main()
{
	TestHashes();
	TestUri();
	TestAmzDate();
	TestHeaderSignature();
	TestPresign();
	if (failures > 0)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
