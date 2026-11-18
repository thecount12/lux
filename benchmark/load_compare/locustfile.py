"""Locust scenario — same mix as Lux LoadTest / k6 in this directory."""

from __future__ import annotations

import os

from locust import HttpUser, between, task


_THINK = float(os.environ.get("LOAD_WAIT", "0.05"))


class CompareUser(HttpUser):
    # Default 50ms think time — match Lux/k6 LOAD_WAIT (use 0 to hammer).
    wait_time = between(_THINK, _THINK)

    @task(5)
    def hello(self) -> None:
        self.client.get("/hello")

    @task(1)
    def echo(self) -> None:
        self.client.post(
            "/echo",
            data='{"n":1}',
            headers={"Content-Type": "application/json"},
        )

    @task(1)
    def graphql(self) -> None:
        self.client.post(
            "/graphql",
            data='{"query":"{ ping }"}',
            headers={"Content-Type": "application/json"},
        )


# Locust reads host from --host / LOAD_HOST via CLI in run_compare.py.
if os.environ.get("LOAD_HOST"):
    CompareUser.host = os.environ["LOAD_HOST"]
