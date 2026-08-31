#include "livekit/core/logging.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {

class IntegrationLogSink final : public livekit::core::LogSinkInterface {
public:
	IntegrationLogSink() : worker_([this] { Run(); }) {}

	~IntegrationLogSink() override {
		{
			std::lock_guard<std::mutex> guard(mutex_);
			stopping_ = true;
		}
		condition_.notify_one();
		worker_.join();
	}

	void OnLog(const livekit::core::LogRecord& record) override {
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (stopping_) {
				return;
			}
			if (records_.size() == kMaximumPendingRecords) {
				records_.pop_front();
				++dropped_records_;
			}
			records_.push_back(record);
		}
		condition_.notify_one();
	}

private:
	static constexpr std::size_t kMaximumPendingRecords = 4096;

	static void Append(std::ostringstream& output, const livekit::core::LogRecord& record) {
		output << "[livekit-sdk][" << livekit::core::LogSourceName(record.source) << "]["
		       << livekit::core::LogLevelName(record.level) << "] " << record.message;
		if (!record.file.empty()) {
			output << " (" << record.file;
			if (record.line > 0) {
				output << ':' << record.line;
			}
			output << ')';
		}
		output << '\n';
	}

	void Run() {
		for (;;) {
			std::vector<livekit::core::LogRecord> batch;
			std::size_t dropped = 0;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				condition_.wait(lock, [this] { return stopping_ || !records_.empty(); });
				batch.reserve(records_.size());
				while (!records_.empty()) {
					batch.push_back(std::move(records_.front()));
					records_.pop_front();
				}
				dropped = std::exchange(dropped_records_, 0);
				if (stopping_ && batch.empty()) {
					break;
				}
			}

			std::ostringstream output;
			if (dropped > 0) {
				output << "[livekit-sdk][integration][warning] dropped " << dropped
				       << " pending log records\n";
			}
			for (const auto& record : batch) {
				Append(output, record);
			}
			std::cerr << output.str();
		}
	}

	std::mutex mutex_;
	std::condition_variable condition_;
	std::deque<livekit::core::LogRecord> records_;
	std::size_t dropped_records_ = 0;
	bool stopping_ = false;
	std::thread worker_;
};

} // namespace

int main(int argc, char** argv) {
	::testing::InitGoogleTest(&argc, argv);

	livekit::core::LogOptions options;
	options.livekit_level = livekit::core::LogLevel::Debug;
	options.webrtc_level = livekit::core::LogLevel::Warning;
	options.websocket_level = livekit::core::LogLevel::Info;
	livekit::core::SetLogOptions(options);
	livekit::core::SetLogSink(std::make_shared<IntegrationLogSink>());

	const int result = RUN_ALL_TESTS();
	livekit::core::SetLogSink(nullptr);
	return result;
}
