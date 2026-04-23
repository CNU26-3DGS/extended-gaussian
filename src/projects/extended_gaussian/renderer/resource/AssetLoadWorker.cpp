#include "AssetLoadWorker.hpp"

#include "GaussianLoader.hpp"

namespace sibr {
	AssetLoadWorker::~AssetLoadWorker()
	{
		stop();
	}

	void AssetLoadWorker::start(int workerCount)
	{
		stop();

		const int actualWorkerCount = std::max(1, workerCount);
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopRequested_ = false;
		}

		for (int i = 0; i < actualWorkerCount; ++i) {
			workers_.emplace_back(&AssetLoadWorker::workerLoop, this);
		}
	}

	void AssetLoadWorker::stop()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopRequested_ = true;
		}
		cv_.notify_all();

		for (auto& worker : workers_) {
			if (worker.joinable()) {
				worker.join();
			}
		}
		workers_.clear();

		std::lock_guard<std::mutex> lock(mutex_);
		while (!pending_.empty()) {
			pending_.pop();
		}
		inFlightCount_ = 0;
	}

	bool AssetLoadWorker::enqueue(const AssetDescriptor& descriptor)
	{
		if (descriptor.id.empty() || descriptor.model_dir.empty()) {
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (stopRequested_) {
				return false;
			}
			pending_.push(descriptor);
		}
		cv_.notify_one();
		return true;
	}

	std::vector<AssetLoadResult> AssetLoadWorker::drainCompleted(size_t maxResults)
	{
		std::vector<AssetLoadResult> results;
		std::lock_guard<std::mutex> lock(mutex_);
		while (!completed_.empty() && results.size() < maxResults) {
			results.emplace_back(std::move(completed_.front()));
			completed_.pop();
		}
		return results;
	}

	size_t AssetLoadWorker::pendingCount() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return pending_.size();
	}

	size_t AssetLoadWorker::inFlightCount() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return inFlightCount_;
	}

	bool AssetLoadWorker::isRunning() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return !workers_.empty() && !stopRequested_;
	}

	void AssetLoadWorker::workerLoop()
	{
		for (;;) {
			AssetDescriptor descriptor;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				cv_.wait(lock, [this]() {
					return stopRequested_ || !pending_.empty();
				});

				if (stopRequested_ && pending_.empty()) {
					return;
				}

				descriptor = pending_.front();
				pending_.pop();
				++inFlightCount_;
			}

			AssetLoadResult result;
			result.id = descriptor.id;
			result.field = GaussianLoader::loadFromModelDir(descriptor.model_dir);
			if (result.field) {
				result.field->name = descriptor.id;
				result.field->path = descriptor.model_dir.string();
			}
			else {
				result.error = "Failed to load asset from model directory: " + descriptor.model_dir.string();
			}

			{
				std::lock_guard<std::mutex> lock(mutex_);
				if (inFlightCount_ > 0) {
					--inFlightCount_;
				}
				completed_.push(std::move(result));
			}
		}
	}
}
