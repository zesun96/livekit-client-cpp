"""Call the example.echo RPC method on a second participant."""

import asyncio

from livekit import rtc
from _common import connection_parser, wait_connected


async def main():
    parser = connection_parser(__doc__)
    parser.add_argument("destination", help="identity printed by rpc_receiver.py")
    parser.add_argument("--payload", default="hello")
    args = parser.parse_args()
    async with rtc.Room() as room:
        await room.connect(args.url, args.token)
        await wait_connected(room)
        response = await room.perform_rpc(args.destination, "example.echo", args.payload)
        print("RPC response:", response)


if __name__ == "__main__":
    asyncio.run(main())
