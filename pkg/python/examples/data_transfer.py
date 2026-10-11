"""Send text, bytes, an optional file, and a reliable data packet."""

import asyncio

from livekit import rtc
from _common import connection_parser, wait_connected


async def main():
    parser = connection_parser(__doc__)
    parser.add_argument("--file", help="optional file to send")
    args = parser.parse_args()
    async with rtc.Room() as room:
        await room.connect(args.url, args.token)
        await wait_connected(room)
        await room.send_text("hello from Python", topic="text-transfer")
        await room.send_bytes(b"livekit\0python", topic="byte-transfer", name="payload.bin")
        if args.file:
            await room.send_file(args.file, topic="file-transfer")
            print("Sent file", args.file)
        await room.publish_data(b"hello", topic="data-transfer")
        print("Sent text, bytes, and reliable data")


if __name__ == "__main__":
    asyncio.run(main())
