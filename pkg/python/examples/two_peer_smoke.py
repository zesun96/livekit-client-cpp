"""Exercise two Python participants against a real LiveKit server.

Requires LIVEKIT_URL, LIVEKIT_TOKEN_A, and LIVEKIT_TOKEN_B. Tokens must join
the same room with distinct identities. Uses synthetic media only.
"""

from __future__ import annotations

import asyncio
import os
import sys
import tempfile
from pathlib import Path

from livekit import rtc


async def take(queue: asyncio.Queue, predicate, timeout: float = 15):
    async def find():
        while True:
            value = await queue.get()
            if predicate(value):
                return value
    return await asyncio.wait_for(find(), timeout)


async def main() -> None:
    url = os.environ["LIVEKIT_URL"]
    async with rtc.Room() as sender, rtc.Room() as receiver:
        data = asyncio.Queue()
        chats = asyncio.Queue()
        streams = asyncio.Queue()
        byte_streams = asyncio.Queue()
        data_tracks = asyncio.Queue()
        tracks = asyncio.Queue()
        joined = asyncio.Queue()
        sender.on("participant_connected", joined.put_nowait)
        receiver.on("data_received", data.put_nowait)
        receiver.on("chat_message_received", chats.put_nowait)
        receiver.on("track_subscribed", lambda publication, participant:
                    tracks.put_nowait((publication, participant)))
        receiver.register_rpc_method("python.echo", lambda request: request.payload + "!")
        receiver.register_text_stream_handler("python.demo", streams.put_nowait)
        receiver.register_byte_stream_handler("python.bytes", byte_streams.put_nowait)
        receiver.on("data_track_published", lambda publication, participant:
                    data_tracks.put_nowait((publication, participant)))

        await sender.connect(url, os.environ["LIVEKIT_TOKEN_A"])
        await receiver.connect(url, os.environ["LIVEKIT_TOKEN_B"])
        sender_id = sender.local_participant.identity
        receiver_id = receiver.local_participant.identity
        assert sender_id != receiver_id
        await take(joined, lambda participant: participant.identity == receiver_id)
        print("PASS connect and participant identity")

        await sender.publish_data(b"hello-data", topic="python.demo",
                                  destination_identities=[receiver_id])
        packet = await take(data, lambda value: value.topic == "python.demo")
        assert packet.data == b"hello-data" and packet.participant_identity == sender_id
        print("PASS reliable data packet")

        reply = await sender.perform_rpc(receiver_id, "python.echo", "hello-rpc")
        assert reply == "hello-rpc!"
        print("PASS RPC round trip")

        message_id, _ = await sender.send_chat_message("hello-chat")
        message = await take(chats, lambda value: value.id == message_id)
        assert message.message == "hello-chat"
        print("PASS chat message")

        writer = await sender.stream_text(
            topic="python.demo", destination_identities=[receiver_id], total_size=11,
        )
        info = await writer.get_info()
        assert info.stream_id == writer.id and info.total_size == 11
        await writer.write("hello ")
        await writer.write("world")
        await writer.close()
        chunks = []
        while True:
            event = await take(streams, lambda value: value.stream_id == info.stream_id)
            if event.type == rtc.StreamEventType.CHUNK:
                chunks.append(event.content)
            if event.type == rtc.StreamEventType.CLOSED:
                break
        assert "".join(chunks) == "hello world"
        print("PASS incremental text stream")

        byte_writer = await sender.stream_bytes(
            topic="python.bytes", name="payload.bin", total_size=4,
            destination_identities=[receiver_id],
        )
        await byte_writer.write(b"ab")
        await byte_writer.write(b"cd")
        await byte_writer.close()
        byte_chunks = []
        while True:
            event = await take(byte_streams, lambda value: value.stream_id == byte_writer.id)
            if event.type == rtc.StreamEventType.CHUNK:
                byte_chunks.append(event.content)
            if event.type == rtc.StreamEventType.CLOSED:
                break
        assert b"".join(byte_chunks) == b"abcd"
        print("PASS incremental byte stream")

        publisher = await sender.publish_data_track(rtc.DataTrackPublishOptions(
            "telemetry", frame_encoding=rtc.DataTrackFrameEncoding.JSON,
        ))
        publication, _ = await take(
            data_tracks, lambda item: item[1].identity == sender_id,
        )
        reader = await receiver.subscribe_data_track(sender_id, publication.sid)
        frame = None
        for _ in range(20):
            await publisher.try_push(b'{"value":1}')
            frame = await reader.read(timeout_ms=500)
            if frame is not None:
                break
        assert frame is not None and frame.data == b'{"value":1}'
        reader.close()
        print("PASS DataTrack publish and subscribe")

        # No microphone, camera, speaker, or screen device is opened.
        audio = rtc.AudioSource(sample_rate=48000, num_channels=1, queue_size_ms=200)
        video = rtc.VideoSource()
        try:
            await audio.capture_frame(bytes(480 * 2))
            await video.capture_i420(bytes(16 * 16 * 3 // 2), 16, 16)
            print("INFO creating synthetic tracks")
            audio_track = await sender.local_participant.create_audio_track("tone", audio)
            video_track = await sender.local_participant.create_video_track("black", video)
            print("INFO publishing synthetic tracks")
            await audio_track.publish(rtc.TrackPublishOptions(source=rtc.TrackSource.MICROPHONE))
            await video_track.publish(rtc.TrackPublishOptions(source=rtc.TrackSource.CAMERA))
            print("INFO waiting for remote subscriptions")
            publications = {}
            while len(publications) < 2:
                publication, participant = await take(
                    tracks, lambda item: item[1].identity == sender_id,
                )
                publications[publication.kind] = publication
            audio_sid = publications[1].sid
            video_sid = publications[2].sid
            audio_reader = await receiver.create_audio_stream(sender_id, audio_sid)
            video_reader = await receiver.create_video_stream(sender_id, video_sid)
            with tempfile.TemporaryDirectory(prefix="livekit-python-recording-") as directory:
                output_path = str(Path(directory) / "remote.wav")
                recorder = await receiver.start_track_recording(
                    sender_id, audio_sid, output_path
                )
                audio_frame = asyncio.create_task(audio_reader.read(timeout_ms=5000))
                video_frame = asyncio.create_task(video_reader.read(timeout_ms=5000))
                for _ in range(60):
                    await audio.capture_frame(bytes(480 * 2))
                    await video.capture_i420(bytes(16 * 16 * 3 // 2), 16, 16)
                    await asyncio.sleep(0.025)
                received_audio = await audio_frame
                received_video = await video_frame
                assert received_audio is not None and received_audio.data
                assert received_video is not None and received_video.data
                print("PASS remote audio and video frame readers")
                await recorder.stop()
                stats = recorder.stats
                assert stats.frames_written > 0 and Path(output_path).stat().st_size > 44
                await recorder.aclose()
                print("PASS remote WAV track recording")
            audio_reader.close()
            video_reader.close()
            assert audio_track.rtc_stats is not None
            print("PASS synthetic audio and video publication")
        finally:
            # Room closure disconnects and releases its tracks before sources close.
            try:
                await sender.aclose()
            finally:
                try:
                    await receiver.aclose()
                finally:
                    audio.close()
                    video.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except Exception as exc:
        print(f"FAIL {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
