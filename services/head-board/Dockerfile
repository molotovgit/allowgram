FROM node:22-bookworm-slim AS web
WORKDIR /web
COPY web/ ./
RUN npm ci --ignore-scripts --no-audit --no-fund && npm run build

FROM python:3.13-slim-bookworm
COPY --from=ghcr.io/astral-sh/uv:0.11.31 /uv /usr/local/bin/uv
WORKDIR /app
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 UV_COMPILE_BYTECODE=1 UV_LINK_MODE=copy
COPY pyproject.toml uv.lock ./
COPY headboard/ ./headboard/
RUN uv sync --frozen --no-dev --no-editable
COPY --from=web /web/dist ./web/dist
ENV PATH="/app/.venv/bin:$PATH" HEAD_DATA_DIR=/data HEAD_PRODUCTION=1
EXPOSE 8080
CMD ["sh", "-c", "exec uvicorn headboard.api:configured_app --factory --host 0.0.0.0 --port ${PORT:-8080} --workers 1 --no-access-log"]
