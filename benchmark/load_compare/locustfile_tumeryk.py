"""Polite Locust root-page load against tumeryk.com."""

from __future__ import annotations

import os

from locust import HttpUser, between, task

_THINK = float(os.environ.get("LOAD_WAIT", "0.5"))


class TumerykUser(HttpUser):
    wait_time = between(_THINK, _THINK + 0.5)

    @task(5)
    def home(self) -> None:
        self.client.get(
            "/",
            headers={
                "User-Agent": "lux-load-compare-locust/1.0 (+https://luxlang.dev; tumeryk compare)"
            },
        )


if os.environ.get("LOAD_HOST"):
    TumerykUser.host = os.environ["LOAD_HOST"]
else:
    TumerykUser.host = "https://www.tumeryk.com"
