import asyncio

import httpx
from test_board import ORIGIN

from headboard.api import create_app


def test_oversized_stream_is_stopped_before_full_body_is_read(tmp_path):
    consumed = []

    async def scenario():
        async def chunks():
            for index in range(8):
                consumed.append(index)
                yield b" " * 400000

        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=create_app(tmp_path)), base_url=ORIGIN
        ) as client:
            result = await client.post(
                "/api/auth/bootstrap",
                content=chunks(),
                headers={"Origin": ORIGIN, "Content-Type": "application/json"},
            )
            assert result.status_code == 413
        assert len(consumed) <= 3, "Server buffered the entire oversized body"

    asyncio.run(scenario())
