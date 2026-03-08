# HTTP Request with Custom Headers

## Overview

The `httpRequest()` native function allows making HTTP requests with custom headers, enabling AWS API calls and other authenticated HTTP operations.

## Function Signature

```lux
httpRequest(method, url, body, headers) -> string or nil
```

## Parameters

- **method** (string): HTTP method
  - Supported: `"GET"`, `"POST"`, `"PUT"`, `"DELETE"`, `"HEAD"`
  
- **url** (string): Complete URL with scheme
  - Must start with `http://` or `https://`
  - Example: `"https://api.example.com/resource"`
  
- **body** (string or nil): Request body
  - Use `nil` for no body (typical for GET)
  - Use string for POST/PUT with data
  
- **headers** (instance or nil): Custom HTTP headers
  - Use instance with fields for headers
  - Field names with underscores become hyphens in HTTP headers
  - Example: `headers.x_amz_date` → `x-amz-date: ...`
  - Use `nil` for no custom headers

## Returns

- **Success**: Response body as string
- **Failure**: `nil`

## Header Name Conversion

Lux identifiers can't contain hyphens, so use underscores in field names. They're automatically converted:

| Lux Field Name | HTTP Header |
|----------------|-------------|
| `Authorization` | `Authorization` |
| `Content_Type` | `Content-Type` |
| `x_amz_date` | `x-amz-date` |
| `x_amz_content_sha256` | `x-amz-content-sha256` |
| `User_Agent` | `User-Agent` |

## Platform Implementation

### POSIX/Linux
Uses `libcurl` with custom header list:
- Standard HTTP/HTTPS support
- Automatic SSL certificate validation
- Follows redirects (up to default limit)
- 30-second timeout

### Plan 9
Manual HTTP protocol implementation:
- Uses `dial()` for TCP connection
- Uses `tlsClient()` for HTTPS
- Manual HTTP header construction
- Connection closed after response

## Examples

### Simple GET Request

```lux
var response = httpRequest("GET", "https://api.example.com/data", nil, nil);
if (response != nil) {
    print response;
}
```

### POST with JSON Body

```lux
class Headers { init() {} }

var headers = Headers();
headers.Content_Type = "application/json";

var body = toJSON({"key": "value"});
var response = httpRequest("POST", "https://api.example.com/create", body, headers);
```

### AWS S3 Authenticated Request

```lux
class Headers { init() {} }

// Get AWS timestamp
var timestamp = getAwsTimestamp();

// Sign request
var authHeader = awsSignRequest(
    "GET",
    "my-bucket.s3.us-east-1.amazonaws.com",
    "/",
    "",
    sha256(""),  // Empty payload hash for GET
    "AKIAIOSFODNN7EXAMPLE",
    "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
    "us-east-1",
    "s3",
    timestamp.amzDate,
    timestamp.dateStamp,
    nil
);

// Build headers
var headers = Headers();
headers.Authorization = authHeader;
headers.x_amz_date = timestamp.amzDate;
headers.x_amz_content_sha256 = sha256("");
headers.Host = "my-bucket.s3.us-east-1.amazonaws.com";

// Make request
var response = httpRequest(
    "GET",
    "https://my-bucket.s3.us-east-1.amazonaws.com/",
    nil,
    headers
);

print response;  // XML response with bucket contents
```

### Custom Headers with Multiple Fields

```lux
class Headers { init() {} }

var headers = Headers();
headers.Authorization = "Bearer token123";
headers.Content_Type = "application/json";
headers.User_Agent = "lux/1.0";
headers.X_Custom_Header = "custom-value";
headers.Accept = "application/json";

var response = httpRequest("GET", "https://api.example.com/data", nil, headers);
```

## Error Handling

The function returns `nil` on error. Common failure cases:

- Invalid URL format
- Network connectivity issues
- DNS resolution failure
- TLS handshake failure (HTTPS)
- Server timeout
- Invalid HTTP status (depends on implementation)

Always check for `nil` before using the response:

```lux
var response = httpRequest("GET", url, nil, headers);
if (response == nil) {
    print "Request failed!";
} else {
    print "Success: " + response;
}
```

## Limitations

### Current Implementation
- No access to HTTP status codes (returns body or nil)
- No access to response headers
- No progress callbacks for large transfers
- Fixed timeout values (30 seconds for POSIX)
- No streaming - entire response loaded into memory

### Security Notes
- HTTPS certificates are validated (POSIX/libcurl)
- No certificate pinning support
- Credentials in code are visible - use environment variables or config files in production
- No automatic retry on transient failures

## See Also

- [AWS_API_GUIDE.md](AWS_API_GUIDE.md) - Using AWS APIs with httpRequest
- [examples/aws_s3_example.lux](examples/aws_s3_example.lux) - Working S3 example
- [examples/aws_helper.lux](examples/aws_helper.lux) - AWS helper classes
