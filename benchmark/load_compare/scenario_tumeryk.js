// Polite k6 root-page load against tumeryk.com
import http from "k6/http";
import { check, sleep } from "k6";

const host = __ENV.LOAD_HOST || "https://www.tumeryk.com";
const vus = Number(__ENV.LOAD_VUS || 5);
const duration = __ENV.LOAD_DURATION || "10s";
const think = Number(__ENV.LOAD_WAIT || 0.5);

export const options = {
  vus,
  duration,
  summaryTrendStats: ["avg", "med", "p(95)", "p(99)"],
};

export default function () {
  const res = http.get(`${host}/`, {
    headers: {
      "User-Agent":
        "lux-load-compare-k6/1.0 (+https://luxlang.dev; tumeryk compare)",
    },
  });
  check(res, { "status 2xx": (x) => x.status >= 200 && x.status < 300 });
  sleep(think + Math.random() * 0.5);
}
