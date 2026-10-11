"""Register an RPC method; run before rpc_caller.py in the same room."""

import asyncio

from livekit import rtc
from _common import connection_parser, run_for, wait_connected


async def main():
    args = connection_parser(__doc__, duration=30).parse_args()
    async with rtc.Room() as room:
        def echo(request):
            print(f"RPC from {request.caller_identity}: {request.payload}")
            return "echo:" + request.payload

        room.register_rpc_method("example.echo", echo)
        await room.connect(args.url, args.token)
        await wait_connected(room)
        print("RPC receiver ready as", room.local_participant.identity)
        await run_for(args.seconds)


if __name__ == "__main__":
    asyncio.run(main())
