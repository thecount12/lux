# AWS API Guide for Lux

## Overview

You can now call AWS APIs directly from Lux using AWS Signature Version 4 authentication. Several new native functions make this possible:

**Available in both Plan 9 and POSIX/Linux versions.**

## New Native Functions

### 1. `sha256(data)` → string
Computes SHA-256 hash of input data, returns hex string.

```lux
var hash = sha256("Hello, World!");
print hash;  // "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"

var emptyHash = sha256("");  // Common for GET requests
```

### 2. `hmacSha256(key, data)` → string  
Computes HMAC-SHA256, returns hex string.

```lux
var hmac = hmacSha256("secret-key", "message");
print hmac;
```

### 3. `getAwsTimestamp()` → instance
Returns current UTC time formatted for AWS requests.

```lux
var timestamp = getAwsTimestamp();
print timestamp.amzDate;     // "20240307T152030Z"
print timestamp.dateStamp;   // "20240307"
```

**Returns:** Instance with two fields:
- `amzDate`: Full timestamp (YYYYMMDDTHHmmssZ format)
- `dateStamp`: Date only (YYYYMMDD format)

### 4. `awsSignRequest(method, host, uri, queryString, payloadHash, accessKey, secretKey, region, service, amzDate, dateStamp, [sessionToken])` → string
Generates AWS Signature V4 Authorization header.

**Parameters:**
- `method`: HTTP method ("GET", "POST", "PUT", etc.)
- `host`: AWS service host (e.g., "my-bucket.s3.us-east-1.amazonaws.com")
- `uri`: URI path (e.g., "/", "/object-key")
- `queryString`: Query parameters (e.g., "list-type=2&prefix=docs")
- `payloadHash`: SHA-256 hash of request body (hex string)
- `accessKey`: AWS access key ID
- `secretKey`: AWS secret access key
- `region`: AWS region (e.g., "us-east-1")
- `service`: AWS service (e.g., "s3", "dynamodb")
- `amzDate`: Timestamp in format "YYYYMMDDTHHmmssZ"
- `dateStamp`: Date in format "YYYYMMDD"
- `sessionToken`: (Optional) Session token for temporary credentials

**Returns:** Authorization header value (string)

### 5. `httpRequest(method, url, body, headers)` → string or nil
Makes HTTP request with custom headers.

**Parameters:**
- `method`: HTTP method ("GET", "POST", "PUT", "DELETE", "HEAD")
- `url`: Full URL including scheme (https://...)
- `body`: Request body as string, or `nil` for no body
- `headers`: Instance with header fields, or `nil` for no custom headers

**Returns:** Response body as string, or `nil` on error

**Header Names:** Use underscores in Lux field names - they're automatically converted to hyphens:
- `headers.x_amz_date` → HTTP header `x-amz-date`
- `headers.Content_Type` → HTTP header `Content-Type`

```lux
class Headers { init() {} }

var headers = Headers();
headers.Authorization = "AWS4-HMAC-SHA256 ...";
headers.x_amz_date = "20240307T120000Z";

var response = httpRequest("GET", "https://my-bucket.s3.us-east-1.amazonaws.com/", nil, headers);
print response;
```

## Complete Working Example
```lux
// 1. Hash the payload
var payloadHash = sha256("");  // Empty for GET

// 2. Generate signature
var authHeader = awsSignRequest(
    "GET",
    "my-bucket.s3.us-east-1.amazonaws.com",
    "/",
    "list-type=2",
    payloadHash,
    "AKIAIOSFODNN7EXAMPLE",
    "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
    "us-east-1",
    "s3",
    "20240307T120000Z",
    "20240307",
    nil
);

// 3. Use in HTTP request
// You would add these headers:
//   Authorization: {authHeader}
//   x-amz-date: 20240307T120000Z
//   x-amz-content-sha256: {payloadHash}
```

## Working with Different AWS Services

### S3 (REST-XML)
```lux
var bucket = "my-bucket";
var host = bucket + ".s3.us-east-1.amazonaws.com";
var payloadHash = sha256("");

var auth = awsSignRequest(
    "GET", host, "/", "list-type=2", 
    payloadHash, accessKey, secretKey,
    "us-east-1", "s3", amzDate, dateStamp, nil
);

// Make request to: https://{host}/?list-type=2
// Headers: Authorization, x-amz-date, x-amz-content-sha256
```

### DynamoDB (JSON)
```lux
var payload = toJSON({"TableName": "Users", "Key": {"id": {"S": "user123"}}});
var payloadHash = sha256(payload);
var host = "dynamodb.us-east-1.amazonaws.com";

var auth = awsSignRequest(
    "POST", host, "/", "",
    payloadHash, accessKey, secretKey,
    "us-east-1", "dynamodb", amzDate, dateStamp, nil
);

// Make request to: https://{host}/
// Headers: Authorization, x-amz-date, x-amz-target: DynamoDB_20120810.GetItem
// Body: {payload}
```

### With STS Temporary Credentials
AWS STS (Security Token Service) provides temporary credentials that include a session token. These work with all AWS services:

```lux
// Temporary credentials from STS AssumeRole, GetSessionToken, etc.
var tempAccessKey = "ASIAIOSFODNN7EXAMPLE";  // Starts with "ASIA" for temp creds
var tempSecretKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYTEMPORARYKEY";
var sessionToken = "FwoGZXIvYXdzE...very-long-token...==";

var auth = awsSignRequest(
    method, host, uri, queryString, payloadHash,
    tempAccessKey, tempSecretKey,  // Use temporary credentials
    region, service,
    amzDate, dateStamp, 
    sessionToken  // REQUIRED for temporary credentials
);

// The session token is automatically included in the signature
// You must also send it as a header: x-amz-security-token: {sessionToken}
```

**Important Notes:**
- Session token is **required** for temporary credentials (request will fail without it)
- Temporary access keys start with `ASIA` (permanent keys start with `AKIA`)
- STS credentials expire (typically 1-12 hours)
- All built-in functions (`s3ListObjects`, `s3GetObject`, `s3PutObject`) also support session tokens as the last parameter

## Example Files

Four example files demonstrate usage:

1. **`examples/aws_crypto_utils.lux`** - Low-level crypto functions
2. **`examples/aws_custom_api.lux`** - Basic AWS API calls
3. **`examples/aws_helper.lux`** - Structured helper classes for S3 and DynamoDB
4. **`examples/aws_sts_example.lux`** - Using temporary credentials from AWS STS

## STS (Temporary Credentials) Support

✅ **Full STS support** - All functions work with temporary credentials from AWS Security Token Service:

- **AssumeRole** - Cross-account access
- **AssumeRoleWithWebIdentity** - Federated users (Cognito, OIDC)
- **GetSessionToken** - MFA-protected operations
- **GetFederationToken** - Temporary access for federated users

When using temporary credentials:
1. You get three values: `AccessKeyId`, `SecretAccessKey`, and `SessionToken`
2. Pass all three to `awsSignRequest()` (session token is the last parameter)
3. The session token is automatically included in the signature calculation
4. Temporary access keys start with `ASIA` (permanent start with `AKIA`)
5. Credentials expire after the specified duration

See [examples/aws_sts_example.lux](examples/aws_sts_example.lux) for complete examples.

## Current Limitations

1. **URL encoding**: S3 object keys and query parameters should be URL-encoded for production use.
2. **Response metadata**: `httpRequest()` currently returns only the response body (not status code or response headers).
3. **Large responses**: Responses are read fully into memory before returning.

## Next Steps

To improve AWS ergonomics further:

1. Add URL encoding helpers to Lux.
2. Add an HTTP variant that returns status code + headers + body.
3. Add higher-level XML helpers for S3 list/get workflows.

You can already make full AWS API calls from Lux today using `getAwsTimestamp()`, `awsSignRequest()`, and `httpRequest()`.

## Platform Differences

Both implementations provide identical Lux APIs, but use different underlying crypto libraries:

- **Plan 9 version** (`vm.c`): Uses Plan 9's `libsec` (sha2_256, hmac_sha2_256)
- **POSIX/Linux version** (`posix/vm.c`): Uses OpenSSL (SHA256, HMAC from `openssl/sha.h` and `openssl/hmac.h`)

The Lux API is identical on both platforms.
