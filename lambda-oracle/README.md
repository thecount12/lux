# Lux SQL Lambda (Oracle, Postgres, MySQL)

Same handler for all three images: raw SQL over HTTP, no stored procedures.

| Image | Dockerfile | `DB_DRIVER` |
|-------|------------|-------------|
| `lux-lambda-oracle` | `Dockerfile-linux-oracle` | `oracle` |
| `lux-lambda-postgres` | `Dockerfile-linux-postgres` | `postgres` |
| `lux-lambda-mysql` | `Dockerfile-linux-mysql` | `mysql` |

Routes (Mangum, same as `lambda/`):

| Method | Path | Body |
|--------|------|------|
| GET | `/health` | — |
| POST | `/query` | `{"sql":"SELECT ..."}` |

Set `DB_URL` at run/deploy time. Do not bake passwords into the image.

```
# Oracle Database Free
DB_URL=system/mypass123@host:1521/FREEPDB1

# PostgreSQL (libpq keyword string)
DB_URL=host=db.internal dbname=app user=app password=secret

# MySQL
DB_URL=host=db.internal;user=root;password=secret;database=app
```

`GET /health` runs `DB_HEALTH_SQL` (`SELECT 1 FROM DUAL` on Oracle, `SELECT 1` otherwise).

## Build and run locally

From the **repo root**. Oracle Instant Client is downloaded during the Oracle image build (needs network).

```bash
docker build -f Dockerfile-linux-oracle -t lux-lambda-oracle .
docker build -f Dockerfile-linux-postgres -t lux-lambda-postgres .
docker build -f Dockerfile-linux-mysql -t lux-lambda-mysql .

docker run --rm -p 9000:8080 \
  -e DB_URL='system/mypass123@host.docker.internal:1521/FREEPDB1' \
  lux-lambda-oracle
```

POST a Lambda event to RIE (not `http://localhost:9000/`):

```bash
# GET /health
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"httpMethod":"GET","path":"/health"}'

# POST /query
curl -s -X POST "http://localhost:9000/2015-03-31/functions/function/invocations" \
  -H "Content-Type: application/json" \
  -d '{"httpMethod":"POST","path":"/query","body":"{\"sql\":\"SELECT 1 FROM DUAL\"}"}'
```

On AWS, put the function in a VPC that can reach the database (port 1521 / 5432 / 3306). A function in the cloud cannot use `10.0.0.22` on your Mac.

SQL comes in as a string (`dbQuery` has no bind parameters). Only send SQL from a trusted caller.

See `lambda/README.md` for Mangum, Function URLs, and deploy.
