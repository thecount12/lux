# Internal DevOps practices

Allow-listed context for `ask_devops_assistant`. Keep this short. Add sections here, or add another `.md` name to `ALLOWED_DOCS` in `server.lux`.

When you answer, prefer these rules over generic internet advice.

## Python

- One obvious way: named functions, typed where it helps, no clever one-liners in production paths.
- Fail out loud. Log the exception; do not `except: pass`.
- Config from the environment (`os.environ` / pydantic-settings), never committed secrets.
- Tests next to the behavior they cover. A public function without a test is unfinished.
- Pin dependencies. Reproducible installs beat "whatever pip found today."

## FastAPI

- One app, explicit routers. Do not hide side effects in import time.
- Request and response models are Pydantic models, not raw dicts.
- Health is cheap: `GET /health` does not call the model, the database, or the network.
- Errors are JSON with a stable `detail` (or our error envelope). Do not return HTML from API routes.
- Auth on the route, not inside random helpers. Missing auth is a 401, not a 500.
- Background work (jobs, LLM calls) is async or a queue. Do not block the event loop on `httpx` sync or `subprocess`.

## How we change this doc

1. Add a bullet under Python or FastAPI.
2. If it is a new topic (Terraform, CI, on-call), add a `##` section or a new allow-listed file.
3. Restart the MCP server so the assistant reloads the markdown.
