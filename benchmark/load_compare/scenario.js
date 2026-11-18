// k6 scenario — same shape as Lux LoadTest / Locust in this directory.
// Env (optional): LOAD_HOST, LOAD_VUS, LOAD_DURATION
import http from "k6/http";
import { check, sleep } from "k6";

const host = __ENV.LOAD_HOST || "http://127.0.0.1:8765";
const vus = Number(__ENV.LOAD_VUS || 10);
const duration = __ENV.LOAD_DURATION || "10s";
const think = Number(__ENV.LOAD_WAIT || 0.05);

export const options = {
  vus,
  duration,
  summaryTrendStats: ["avg", "med", "p(95)", "p(99)"],
};

export default function () {
  // Weight ~5/7 GET hello, ~1/7 POST echo, ~1/7 GraphQL (same mix as Lux/Locust).
  const r = Math.random() * 7;
  let res;
  if (r < 5) {
    res = http.get(`${host}/hello`);
  } else if (r < 6) {
    res = http.post(`${host}/echo`, '{"n":1}', {
      headers: { "Content-Type": "application/json" },
    });
  } else {
    res = http.post(
      `${host}/graphql`,
      '{"query":"{ ping }"}',
      { headers: { "Content-Type": "application/json" } }
    );
  }
  check(res, { "status 2xx": (x) => x.status >= 200 && x.status < 300 });
  if (think > 0) sleep(think);
}
