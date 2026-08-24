#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::optional<std::string> Argument(int argc, char** argv, std::string_view name) {
	for (int index = 1; index + 1 < argc; ++index) {
		if (std::string_view(argv[index]) == name) {
			return argv[index + 1];
		}
	}
	return std::nullopt;
}

class PeerDelegate final : public livekit::RoomDelegate {
public:
	PeerDelegate(std::string sender_identity, std::string topic, std::vector<std::uint8_t> payload)
	    : sender_identity_(std::move(sender_identity)), topic_(std::move(topic)),
	      payload_(std::move(payload)) {}

	void onUserPacketReceived(livekit::Room&, const livekit::UserDataPacketEvent& event) override {
		if (event.participant != nullptr && event.participant->identity() == sender_identity_ &&
		    event.topic == topic_ && event.data == payload_) {
			data_received_.store(true);
		}
	}

	bool data_received() const { return data_received_.load(); }

private:
	std::string sender_identity_;
	std::string topic_;
	std::vector<std::uint8_t> payload_;
	std::atomic<bool> data_received_{false};
};

} // namespace

int main(int argc, char** argv) {
	const auto url = Argument(argc, argv, "--url");
	const auto token = Argument(argc, argv, "--token");
	const auto sender_identity = Argument(argc, argv, "--sender-identity");
	const auto ready_file = Argument(argc, argv, "--ready-file");
	if (!url || !token || !sender_identity || !ready_file) {
		std::cerr << "Usage: livekit_official_cpp_e2ee_peer --url <url> --token <token> "
		             "--sender-identity <identity> --ready-file <path>\n";
		return 2;
	}

	constexpr std::string_view kSharedPassphrase = "livekit-cpp-official-cpp-e2ee";
	const std::vector<std::uint8_t> expected_data{'c', 'p', 'p', '-', 'o', 'f', 'f', 'i', 'c',
	                                              'i', 'a', 'l', '-', 'e', '2', 'e', 'e'};
	const std::vector<std::uint8_t> acknowledgement{'o', 'f', 'f', 'i', 'c', 'i', 'a',
	                                                'l', '-', 'c', 'p', 'p', '-', 'e',
	                                                '2', 'e', 'e', '-', 'o', 'k'};

	try {
		livekit::initialize(livekit::LogLevel::Warn);
		{
			PeerDelegate delegate(*sender_identity, "cpp-official-e2ee-interop", expected_data);
			livekit::Room room;
			room.setDelegate(&delegate);
			livekit::RoomOptions options;
			livekit::E2EEOptions e2ee;
			e2ee.key_provider_options.shared_key =
			    std::vector<std::uint8_t>(kSharedPassphrase.begin(), kSharedPassphrase.end());
			options.encryption = e2ee;
			if (!room.connect(*url, *token, options)) {
				throw std::runtime_error("official SDK failed to connect");
			}

			std::atomic<std::uint64_t> audio_frames{0};
			std::atomic<std::uint64_t> video_frames{0};
			room.setOnAudioFrameCallback(*sender_identity, "cpp-e2ee-official-audio",
			                             [&](const livekit::AudioFrame& frame) {
				                             if (!frame.data().empty() && frame.sampleRate() > 0) {
					                             audio_frames.fetch_add(1);
				                             }
			                             });
			room.setOnVideoFrameCallback(*sender_identity, "cpp-e2ee-official-video",
			                             [&](const livekit::VideoFrame& frame, std::int64_t) {
				                             if (frame.width() > 0 && frame.height() > 0 &&
				                                 frame.dataSize() > 0) {
					                             video_frames.fetch_add(1);
				                             }
			                             });

			std::ofstream ready(*ready_file, std::ios::binary | std::ios::trunc);
			ready << "ready\n";
			if (!ready.good()) {
				throw std::runtime_error("unable to create peer readiness marker");
			}
			ready.close();

			const auto deadline = std::chrono::steady_clock::now() + 35s;
			while (
			    (audio_frames.load() < 3 || video_frames.load() < 3 || !delegate.data_received()) &&
			    std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(20ms);
			}
			if (audio_frames.load() < 3 || video_frames.load() < 3 || !delegate.data_received()) {
				throw std::runtime_error("timed out waiting for decrypted audio, video, and data");
			}

			auto participant = room.localParticipant().lock();
			if (!participant) {
				throw std::runtime_error("official SDK local participant is unavailable");
			}
			participant->publishData(acknowledgement, true, {*sender_identity},
			                         "official-cpp-e2ee-interop");
			std::this_thread::sleep_for(1s);
			room.clearOnAudioFrameCallback(*sender_identity, "cpp-e2ee-official-audio");
			room.clearOnVideoFrameCallback(*sender_identity, "cpp-e2ee-official-video");
			room.disconnect();
		}
		livekit::shutdown();
		std::cout << "PASS official C++ v1.8.0 E2EE audio, video, and data interoperability\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Official C++ peer failed: " << error.what() << '\n';
		livekit::shutdown();
		return 1;
	}
}
