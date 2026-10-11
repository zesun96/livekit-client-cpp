"""Join a LiveKit room and print incoming data and participant events.

Requires LIVEKIT_URL and LIVEKIT_TOKEN. The token must grant room join.
"""

import asyncio
import os

from livekit import rtc


async def main() -> None:
    async with rtc.Room() as room:
        room.on("participant_connected", lambda participant:
                print(f"joined: {participant.identity}"))
        room.on("data_received", lambda packet:
                print(f"data from {packet.participant_identity}: {packet.data!r}"))
        room.on("chat_message_received", lambda message:
                print(f"chat from {message.participant_identity}: {message.message}"))

        await room.connect(os.environ["LIVEKIT_URL"], os.environ["LIVEKIT_TOKEN"])
        print(f"connected: room={room.name} identity={room.local_participant.identity}")
        await room.local_participant.publish_data(b"hello from Python", topic="greeting")
        await asyncio.sleep(10)
        await room.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
