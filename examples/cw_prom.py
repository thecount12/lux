#!/usr/bin/env python3
"""CloudWatch -> Prometheus. Scrape :9106/metrics or set PUSHGATEWAY_URL.

Loads the same .env / shared-credentials dump Lux uses (aws_access_key_id, ...).
Otherwise boto3's default chain (env, profile, instance role) applies.

    pip install boto3
    python3 examples/cw_prom.py
    LAMBDA_NAME=my-function python3 examples/cw_prom.py
"""
from __future__ import print_function

import os
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.request import Request, urlopen

import boto3


def load_dotenv(path):
    if not os.path.isfile(path):
        return
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#") or line.startswith("["):
                continue
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip().strip('"').strip("'")
            os.environ.setdefault(key, value)
            os.environ.setdefault(key.upper(), value)


for candidate in (".env", os.path.join("examples", ".env"), os.path.join("..", ".env")):
    load_dotenv(candidate)

REGION = os.environ.get("AWS_REGION") or os.environ.get("region") or "us-east-1"
PORT = int(os.environ.get("PROM_PORT", "9106"))
LOOKBACK = int(os.environ.get("CW_LOOKBACK", "600"))
PERIOD = int(os.environ.get("CW_PERIOD", "60"))
PUSHGATEWAY = os.environ.get("PUSHGATEWAY_URL", "")
LAMBDA_NAME = os.environ.get("LAMBDA_NAME", "my-function")

METRICS = [
    {
        "id": "lambda_invocations",
        "namespace": "AWS/Lambda",
        "metric": "Invocations",
        "stat": "Sum",
        "dims": {"FunctionName": LAMBDA_NAME},
    },
    {
        "id": "lambda_errors",
        "namespace": "AWS/Lambda",
        "metric": "Errors",
        "stat": "Sum",
        "dims": {"FunctionName": LAMBDA_NAME},
    },
    {
        "id": "lambda_duration",
        "namespace": "AWS/Lambda",
        "metric": "Duration",
        "stat": "Average",
        "dims": {"FunctionName": LAMBDA_NAME},
    },
]


def prom_name(namespace, metric):
    return (
        namespace.replace("AWS/", "aws_").replace("/", "_").lower()
        + "_"
        + metric.replace(".", "_").lower()
    )


def labels(dims, stat, region):
    parts = ['region="%s"' % region, 'stat="%s"' % stat]
    for key, value in dims.items():
        parts.append('%s="%s"' % (key.lower(), value))
    return "{" + ",".join(parts) + "}"


def fetch():
    client = boto3.client("cloudwatch", region_name=REGION)
    end = int(time.time())
    start = end - LOOKBACK
    queries = []
    for metric in METRICS:
        queries.append(
            {
                "Id": metric["id"],
                "MetricStat": {
                    "Metric": {
                        "Namespace": metric["namespace"],
                        "MetricName": metric["metric"],
                        "Dimensions": [
                            {"Name": key, "Value": value}
                            for key, value in metric["dims"].items()
                        ],
                    },
                    "Period": PERIOD,
                    "Stat": metric["stat"],
                },
                "ReturnData": True,
            }
        )
    response = client.get_metric_data(
        MetricDataQueries=queries,
        StartTime=start,
        EndTime=end,
        ScanBy="TimestampDescending",
    )
    by_id = {row["Id"]: row for row in response.get("MetricDataResults", [])}
    lines = [
        "# HELP cloudwatch_scrape_success 1 if last CloudWatch pull worked",
        "# TYPE cloudwatch_scrape_success gauge",
        "cloudwatch_scrape_success 1",
    ]
    for metric in METRICS:
        name = prom_name(metric["namespace"], metric["metric"])
        values = by_id.get(metric["id"], {}).get("Values") or []
        value = values[0] if values else 0
        lines.append("# HELP %s CloudWatch %s %s" % (name, metric["namespace"], metric["metric"]))
        lines.append("# TYPE %s gauge" % name)
        lines.append("%s%s %s" % (name, labels(metric["dims"], metric["stat"], REGION), value))
    return "\n".join(lines) + "\n"


def push(text):
    if not PUSHGATEWAY:
        return
    url = PUSHGATEWAY.rstrip("/") + "/metrics/job/cloudwatch/region/%s" % REGION
    request = Request(url, data=text.encode("utf-8"), method="PUT")
    request.add_header("Content-Type", "text/plain; version=0.0.4")
    urlopen(request, timeout=10)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] != "/metrics":
            self.send_error(404)
            return
        try:
            body = fetch()
            push(body)
            payload = body.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as exc:
            err = ("cloudwatch_scrape_success 0\n# %s\n" % exc).encode("utf-8")
            self.send_response(500)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(err)

    def log_message(self, *_args):
        pass


if __name__ == "__main__":
    print("CloudWatch exporter on http://0.0.0.0:%s/metrics region=%s function=%s" % (
        PORT, REGION, LAMBDA_NAME))
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
