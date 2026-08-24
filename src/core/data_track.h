#pragma once

#ifndef _LKC_CORE_DATA_TRACK_INTERNAL_H_
#define _LKC_CORE_DATA_TRACK_INTERNAL_H_

#include "livekit/core/data_track.h"

#include <functional>
#include <map>
#include <mutex>

namespace livekit {
namespace core {

class LocalDataTrack final : public LocalDataTrackInterface {
public:
	using PushHandler = std::function<DataTrackError(uint16_t, const DataTrackFrame&)>;
	using UnpublishHandler = std::function<DataTrackError(uint16_t)>;

	LocalDataTrack(DataTrackInfo info, PushHandler push, UnpublishHandler unpublish);
	~LocalDataTrack() override = default;

	DataTrackInfo Info() const override;
	bool IsPublished() const override;
	DataTrackError TryPush(const DataTrackFrame& frame) override;
	DataTrackError Unpublish() override;

	void UpdateInfo(DataTrackInfo info);
	void MarkUnpublished();

private:
	mutable std::mutex mutex_;
	DataTrackInfo info_;
	bool published_ = true;
	PushHandler push_;
	UnpublishHandler unpublish_;
};

class RemoteDataTrack final : public RemoteDataTrackInterface,
                              public std::enable_shared_from_this<RemoteDataTrack> {
public:
	using SubscriptionHandler =
	    std::function<bool(const std::string&, bool, const DataTrackSubscriptionOptions&)>;

	RemoteDataTrack(DataTrackInfo info, std::string publisher_identity,
	                SubscriptionHandler subscription);
	~RemoteDataTrack() override;

	DataTrackInfo Info() const override;
	bool IsPublished() const override;
	std::string PublisherIdentity() const override;
	std::shared_ptr<DataTrackReader> Subscribe(DataTrackSubscriptionOptions options = {}) override;
	bool SetSubscriptionOptions(DataTrackSubscriptionOptions options) override;

	void UpdateInfo(DataTrackInfo info);
	bool ResendSubscription();
	void MarkUnpublished();
	void PushFrame(DataTrackFrame frame);

private:
	void ReaderClosed(const std::shared_ptr<DataTrackReader::Impl>& reader);
	void PruneReadersLocked();

	mutable std::mutex mutex_;
	DataTrackInfo info_;
	std::string publisher_identity_;
	bool published_ = true;
	bool subscribed_ = false;
	DataTrackSubscriptionOptions subscription_options_;
	SubscriptionHandler subscription_;
	std::vector<std::weak_ptr<DataTrackReader::Impl>> readers_;
};

} // namespace core
} // namespace livekit

#endif // _LKC_CORE_DATA_TRACK_INTERNAL_H_
