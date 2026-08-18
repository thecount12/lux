# Lux on AWS Lambda

A custom runtime that serves a Lux `Server` the way [Mangum](https://github.com/jordaneremieff/mangum) serves FastAPI: same routes locally and on Lambda.

Put API Gateway (REST or HTTP API) or a Function URL in **proxy** mode in front of the function. The handler is `lambda/handler.lux`.

## Layout

| Path | Role |
|------|------|
| `lambda/bootstrap` | Custom runtime loop: fetch event → run Lux → post response |
| `lambda/handler.lux` | Routes + `Mangum(server)` (cwd `/var/task`) |
| `lib/mangum.lux` | Event → `server.handle()` → Lambda proxy JSON |
| `../Dockerfile.lambda` | Amazon Linux build + `provided.al2023` image |
| `../examples/lambda_web.lux` | Same routes as a local HTTP server |
| `../examples/lambda_raw.lux` | Original handler: event file in, proxy JSON out, no Mangum |

Inside the image:

```
/var/runtime/bootstrap      ← this folder’s bootstrap
/var/task/lux
/var/task/lambda/handler.lux
/var/task/lib/mangum.lux
```

`import` paths are relative to the **process working directory**, which must be `/var/task`.

## Routes (sample app)

Both `handler.lux` and `examples/lambda_web.lux` register:

| Method | Path | Response |
|--------|------|----------|
| GET | `/` | HTML |
| GET | `/health` | `{"ok":true}` |
| GET / POST | `/echo` | JSON with `method`, `path`, `body` |

Edit `handler.lux` like any other Lux `Server`:

```lux
fun handleHome(req, res) {
    res.html("<h1>Hello from Lux</h1>");
}

var server = Server(0);   /* port unused under Mangum */
server.get("/", handleHome);
Mangum(server);
```

## Local (no Docker)

From `posix/`:

```bash
# Real HTTP server — curl like a website
./lux ../examples/lambda_web.lux
curl http://127.0.0.1:8084/
curl http://127.0.0.1:8084/health

# One request, no listen (same dispatch as Lambda)
./lux ../examples/lambda_web.lux --once
```

If `/tmp/lambda_event.json` exists, `lambda_web.lux` calls `Mangum(server)` instead of `start()`. `--once` still wins over that file.

## Without Mangum

`examples/lambda_raw.lux` is the original bootstrap handler: read `/tmp/lambda_event.json`, `parseJSON`, write `/tmp/lambda_response.json`. No `Server`, no `lib/mangum.lux`.

```bash
printf '%s' '{"hello":"world"}' > /tmp/lambda_event.json
cd posix && ./lux ../examples/lambda_raw.lux
cat /tmp/lambda_response.json
```

To run that style on Lambda, copy it over `lambda/handler.lux`.

## Docker: build and run

From the **repo root**:

```bash
docker build -f Dockerfile.lambda -t lux-lambda .
docker run --rm -p 9000:8080 lux-lambda
```

The image is `public.ecr.aws/lambda/provided:al2023`. Port **8080 in the container** is the [Runtime Interface Emulator](https://docs.aws.amazon.com/lambda/latest/dg/images-test.html) (RIE), mapped to **9000** on the host.

You do **not** `curl http://localhost:9000/`. You POST a Lambda **event** to the invoke URL.

## Test with curl

```bash
# GET /
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"httpMethod":"GET","path":"/","headers":{"Host":"localhost"}}'

# GET /health
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"httpMethod":"GET","path":"/health"}'

# POST /echo
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"httpMethod":"POST","path":"/echo","body":"hello"}'
```

The HTTP response from RIE is the function result, not a web page:

```json
{
  "statusCode": 200,
  "headers": { "content-type": "text/html; charset=utf-8" },
  "body": "<!DOCTYPE html>...",
  "isBase64Encoded": false
}
```

Print only the HTML:

```bash
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"httpMethod":"GET","path":"/"}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["body"])'
```

HTTP API and Lambda Function URLs use the v2 shape (`rawPath` + `requestContext.http.method`):

```bash
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"version":"2.0","rawPath":"/health","requestContext":{"http":{"method":"GET"}}}'
```

`lib/mangum.lux` accepts both v1 (`httpMethod` / `path`) and v2.

## How an invoke works

1. RIE (or real Lambda) gives `bootstrap` the next event.
2. `bootstrap` writes it to `/tmp/lambda_event.json` and runs `/var/task/lux /var/task/lambda/handler.lux`.
3. `Mangum(server)` reads that file, calls `server.handle(...)`, writes `/tmp/lambda_response.json`.
4. `bootstrap` POSTs that file back as the invocation result.

Logs from Lux are written to CloudWatch (bootstrap copies `/tmp/lux_stdout.log` and `/tmp/lux_stderr.log`). Handler failures become HTTP 500.

## Deploy

Build the image and push to ECR, then create a **container image** Lambda (custom runtime is already in `provided.al2023`).

```bash
docker build -f Dockerfile.lambda -t lux-lambda .
docker tag lux-lambda:latest <account>.dkr.ecr.<region>.amazonaws.com/lux-lambda:latest
docker push <account>.dkr.ecr.<region>.amazonaws.com/lux-lambda:latest
```

Then:

- Function package type: **Image**
- Memory / timeout: start at 256 MB / 10 s (cold start compiles the script each invoke)
- Trigger: **Function URL** (auth NONE or AWS_IAM) or API Gateway **proxy** integration

Rebuild and push after changing the Dockerfile. A running function keeps the old image until you update the code.

If you zip a custom runtime instead of using the Dockerfile, `bootstrap` must be at the **zip root** (not inside a `lambda/` folder) and executable (`chmod 755 bootstrap`). Include `lux`, `lambda/handler.lux`, and `lib/mangum.lux` as well.

## Troubleshooting

**`/bin/sh: /var/runtime/bootstrap: Permission denied`**

Lambda found the bootstrap path but could not exec it. Common causes:

1. `/var/runtime/bootstrap` was a **directory** in the base image and `COPY` placed the script *inside* it. The Dockerfile now `rm -rf` that path first. Rebuild and redeploy.
2. The script was not executable. `lambda/bootstrap` is `0755` in git; the image `chmod 755`s it. For a zip: `chmod 755 bootstrap` before zipping from the **repo root**, not from `lambda/` as a nested folder.
3. Image not updated: `docker build -f Dockerfile.lambda -t lux-lambda .` then push and `aws lambda update-function-code`.

Confirm inside a local container:

```bash
docker run --rm --entrypoint ls lux-lambda -l /var/runtime/bootstrap /var/task/bootstrap /var/task/lux
```

You want a **file** (`-rwxr-xr-x`), not `drwx`.

**Browser downloads a file / curl prints `{"statusCode":...}`**

The Function URL did not unwrap the Lambda proxy JSON (often because the runtime response had no `Content-Type: application/json`). Rebuild with the current `lambda/bootstrap`, which sets that header. After a good deploy, `curl -i` on the Function URL should show `HTTP/2 200` and `content-type: text/html`, not a JSON blob.

**`lux handler execution failed`**

`lux` exited non-zero. Look at the same CloudWatch stream for the real error (import path, missing `.so`, exec format). Typical causes:

- Image missing `libcurl` / OpenSSL — the Dockerfile now `dnf install`s them
- Architecture mismatch — Dockerfile pins `linux/amd64`. If the function is **arm64** in the console, either switch the function to x86_64 or build with `--platform linux/arm64`
- Missing `lib/mangum.lux` in `/var/task/lib/` — the Dockerfile copies it; rebuild

```bash
docker run --rm --entrypoint /bin/sh lux-lambda -c '/var/task/lux /var/task/lambda/handler.lux; echo exit:$?'
```

(That fails without a Lambda event file; you still see import/`ldd` errors.)

```bash
docker run --rm --entrypoint ldd lux-lambda /var/task/lux
```

No `not found` lines.

## Limits

- Same as `Server`: GET and POST routes, exact path match, query string stripped.
- Text responses (HTML, JSON, CSS, JS). Binary bodies are not base64-encoded.
- API Gateway `isBase64Encoded` request bodies are passed through, not decoded.
- Each invoke starts a new `lux` process (see `bootstrap`).
