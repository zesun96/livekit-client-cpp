"""Connect with a credential callback; replace env lookup with a token service."""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main():
    args = connection_parser(__doc__, duration=5).parse_args()

    async def credentials(request):
        print("Fetch token; forced refresh:", request.force_refresh)
        return rtc.TokenSourceResponse(args.url, args.token)

    async with rtc.Room() as room:
        await room.connect_with_token_source(credentials)
        await wait_connected(room)
        print("Connected as", room.local_participant.identity)
        await run_for(args.seconds)


if __name__ == "__main__":
    asyncio.run(main())
