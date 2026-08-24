#include "data_track.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <utility>

namespace livekit {
namespace core {

class DataTrackReader::Impl {
public:
	explicit Impl(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

	bool Read(DataTrackFrame& frame, std::optional<std::chrono::milliseconds> timeout) {
		std::unique_lock<std::mutex> guard(mutex_);
		if (timeout) {
			if (!cv_.wait_for(guard, *timeout, [this] { return closed_ || !frames_.empty(); })) {
				return false;
			}
		} else {
			cv_.wait(guard, [this] { return closed_ || !frames_.empty(); });
		}
		if (frames_.empty()) {
			return false;
		}
		frame = std::move(frames_.front());
		frames_.pop_front();
		return true;
	}

	bool TryRead(DataTrackFrame& frame) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (frames_.empty()) {
			return false;
		}
		frame = std::move(frames_.front());
		frames_.pop_front();
		return true;
	}

	void Push(DataTrackFrame frame) {
		std::lock_guard<std::mutex> guard(mutex_);
		if (closed_) {
			return;
		}
		if (frames_.size() >= capacity_) {
			frames_.pop_front();
			++dropped_frames_;
		}
		frames_.push_back(std::move(frame));
		cv_.notify_one();
	}

	void Close() {
		std::function<void()> callback;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			if (closed_) {
				return;
			}
			closed_ = true;
			frames_.clear();
			callback = std::move(on_close_);
		}
		cv_.notify_all();
		if (callback) {
			callback();
		}
	}

	bool IsClosed() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return closed_;
	}

	std::size_t DroppedFrames() const {
		std::lock_guard<std::mutex> guard(mutex_);
		return dropped_frames_;
	}

	void SetCloseHandler(std::function<void()> handler) {
		std::lock_guard<std::mutex> guard(mutex_);
		on_close_ = std::move(handler);
	}

private:
	const std::size_t capacity_;
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<DataTrackFrame> frames_;
	std::size_t dropped_frames_ = 0;
	bool closed_ = false;
	std::function<void()> on_close_;
};

DataTrackReader::DataTrackReader(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

DataTrackReader::~DataTrackReader() { Close(); }

bool DataTrackReader::Read(DataTrackFrame& frame) {
	return impl_ && impl_->Read(frame, std::nullopt);
}

bool DataTrackReader::ReadFor(DataTrackFrame& frame, std::chrono::milliseconds timeout) {
	return impl_ && timeout >= std::chrono::milliseconds::zero() && impl_->Read(frame, timeout);
}

bool DataTrackReader::TryRead(DataTrackFrame& frame) { return impl_ && impl_->TryRead(frame); }

void DataTrackReader::Close() {
	if (impl_) {
		impl_->Close();
	}
}

bool DataTrackReader::IsClosed() const { return !impl_ || impl_->IsClosed(); }

std::size_t DataTrackReader::DroppedFrames() const { return impl_ ? impl_->DroppedFrames() : 0; }

LocalDataTrack::LocalDataTrack(DataTrackInfo info, PushHandler push, UnpublishHandler unpublish)
    : info_(std::move(info)), push_(std::move(push)), unpublish_(std::move(unpublish)) {}

DataTrackInfo LocalDataTrack::Info() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return info_;
}

bool LocalDataTrack::IsPublished() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return published_;
}

DataTrackError LocalDataTrack::TryPush(const DataTrackFrame& frame) {
	uint16_t handle = 0;
	PushHandler push;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_) {
			return {DataTrackErrorCode::Unpublished, "data track is unpublished"};
		}
		handle = info_.publisher_handle;
		push = push_;
	}
	return push ? push(handle, frame)
	            : DataTrackError{DataTrackErrorCode::Disconnected, "room is disconnected"};
}

DataTrackError LocalDataTrack::Unpublish() {
	uint16_t handle = 0;
	UnpublishHandler unpublish;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_) {
			return {DataTrackErrorCode::Unpublished, "data track is already unpublished"};
		}
		handle = info_.publisher_handle;
		unpublish = unpublish_;
	}
	auto result = unpublish
	                  ? unpublish(handle)
	                  : DataTrackError{DataTrackErrorCode::Disconnected, "room is disconnected"};
	if (!result) {
		MarkUnpublished();
	}
	return result;
}

void LocalDataTrack::UpdateInfo(DataTrackInfo info) {
	std::lock_guard<std::mutex> guard(mutex_);
	info_ = std::move(info);
}

void LocalDataTrack::MarkUnpublished() {
	std::lock_guard<std::mutex> guard(mutex_);
	published_ = false;
	push_ = {};
	unpublish_ = {};
}

RemoteDataTrack::RemoteDataTrack(DataTrackInfo info, std::string publisher_identity,
                                 SubscriptionHandler subscription)
    : info_(std::move(info)), publisher_identity_(std::move(publisher_identity)),
      subscription_(std::move(subscription)) {}

RemoteDataTrack::~RemoteDataTrack() { MarkUnpublished(); }

DataTrackInfo RemoteDataTrack::Info() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return info_;
}

bool RemoteDataTrack::IsPublished() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return published_;
}

std::string RemoteDataTrack::PublisherIdentity() const {
	std::lock_guard<std::mutex> guard(mutex_);
	return publisher_identity_;
}

std::shared_ptr<DataTrackReader> RemoteDataTrack::Subscribe(DataTrackSubscriptionOptions options) {
	if (options.buffer_capacity == 0) {
		options.buffer_capacity = 1;
	}
	if (options.max_partial_frames == 0) {
		options.max_partial_frames = 1;
	}
	auto impl = std::make_shared<DataTrackReader::Impl>(options.buffer_capacity);
	bool send_subscription = false;
	std::string sid;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_) {
			return nullptr;
		}
		PruneReadersLocked();
		send_subscription = !subscribed_;
		sid = info_.sid;
		subscription_options_ = options;
		readers_.push_back(impl);
	}
	if (send_subscription && (!subscription_ || !subscription_(sid, true, options))) {
		impl->Close();
		return nullptr;
	}
	{
		std::lock_guard<std::mutex> guard(mutex_);
		subscribed_ = true;
	}
	auto weak_self = weak_from_this();
	std::weak_ptr<DataTrackReader::Impl> weak_impl = impl;
	impl->SetCloseHandler([weak_self, weak_impl] {
		if (auto self = weak_self.lock()) {
			if (auto reader = weak_impl.lock()) {
				self->ReaderClosed(reader);
			}
		}
	});
	return std::shared_ptr<DataTrackReader>(new DataTrackReader(std::move(impl)));
}

bool RemoteDataTrack::SetSubscriptionOptions(DataTrackSubscriptionOptions options) {
	if (options.buffer_capacity == 0 || options.max_partial_frames == 0) {
		return false;
	}
	std::string sid;
	bool subscribed = false;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_) {
			return false;
		}
		subscription_options_ = options;
		sid = info_.sid;
		subscribed = subscribed_;
	}
	return !subscribed || (subscription_ && subscription_(sid, true, options));
}

void RemoteDataTrack::UpdateInfo(DataTrackInfo info) {
	std::string sid;
	DataTrackSubscriptionOptions options;
	bool resubscribe = false;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		resubscribe = published_ && subscribed_ && info_.sid != info.sid;
		info_ = std::move(info);
		sid = info_.sid;
		options = subscription_options_;
	}
	if (resubscribe && subscription_) {
		subscription_(sid, true, options);
	}
}

bool RemoteDataTrack::ResendSubscription() {
	std::string sid;
	DataTrackSubscriptionOptions options;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_ || !subscribed_) {
			return true;
		}
		sid = info_.sid;
		options = subscription_options_;
	}
	return subscription_ && subscription_(sid, true, options);
}

void RemoteDataTrack::MarkUnpublished() {
	std::vector<std::shared_ptr<DataTrackReader::Impl>> readers;
	std::string sid;
	bool unsubscribe = false;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_) {
			return;
		}
		published_ = false;
		sid = info_.sid;
		unsubscribe = subscribed_;
		subscribed_ = false;
		for (auto& weak : readers_) {
			if (auto reader = weak.lock()) {
				readers.push_back(std::move(reader));
			}
		}
		readers_.clear();
	}
	if (unsubscribe && subscription_) {
		subscription_(sid, false, {});
	}
	for (const auto& reader : readers) {
		reader->SetCloseHandler({});
		reader->Close();
	}
}

void RemoteDataTrack::PushFrame(DataTrackFrame frame) {
	std::vector<std::shared_ptr<DataTrackReader::Impl>> readers;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!published_ || !subscribed_) {
			return;
		}
		PruneReadersLocked();
		for (auto& weak : readers_) {
			if (auto reader = weak.lock()) {
				readers.push_back(std::move(reader));
			}
		}
	}
	for (std::size_t index = 0; index < readers.size(); ++index) {
		readers[index]->Push(index + 1 == readers.size() ? std::move(frame) : frame);
	}
}

void RemoteDataTrack::ReaderClosed(const std::shared_ptr<DataTrackReader::Impl>& reader) {
	std::string sid;
	bool unsubscribe = false;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		readers_.erase(std::remove_if(readers_.begin(), readers_.end(),
		                              [&reader](const auto& weak) {
			                              auto current = weak.lock();
			                              return !current || current == reader;
		                              }),
		               readers_.end());
		if (readers_.empty() && subscribed_) {
			subscribed_ = false;
			sid = info_.sid;
			unsubscribe = published_;
		}
	}
	if (unsubscribe && subscription_) {
		subscription_(sid, false, {});
	}
}

void RemoteDataTrack::PruneReadersLocked() {
	readers_.erase(std::remove_if(readers_.begin(), readers_.end(),
	                              [](const auto& weak) { return weak.expired(); }),
	               readers_.end());
}

} // namespace core
} // namespace livekit
