"""Write an incremental text stream in three chunks."""

import asyncio

from livekit import rtc
from _common import connection_parser, wait_connected


async def main():
    args = connection_parser(__doc__).parse_args()
    async with rtc.Room() as room:
        await room.connect(args.url, args.token)
        await wait_connected(room)
        writer = await room.stream_text(topic="python-incremental",
                                        attributes={"source": "python"}, total_size=11)
        print("Stream ID:", (await writer.get_info()).stream_id)
        for chunk in ("hello", " ", "world"):
            await writer.write(chunk)
        await writer.close()
        print("Stream complete")


if __name__ == "__main__":
    asyncio.run(main())
