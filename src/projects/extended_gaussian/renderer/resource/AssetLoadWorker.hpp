#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"
#include "GaussianField.hpp"
#include "ManifestStore.hpp"

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace sibr {
	struct AssetLoadResult {
		AssetId id;
		GaussianField::Ptr field;
		std::string error;
	};

	class SIBR_EXTENDED_GAUSSIAN_EXPORT AssetLoadWorker {
	public:
		AssetLoadWorker() = default;
		~AssetLoadWorker();

		void start(int workerCount = 1);
		void stop();

		bool enqueue(const AssetDescriptor& descriptor);
		std::vector<AssetLoadResult> drainCompleted(size_t maxResults = std::numeric_limits<size_t>::max());

		size_t pendingCount() const;
		size_t inFlightCount() const;
		bool isRunning() const;

	private:
		void workerLoop();

		mutable std::mutex mutex_;
		std::condition_variable cv_;
		std::queue<AssetDescriptor> pending_;
		std::queue<AssetLoadResult> completed_;
		std::vector<std::thread> workers_;
		bool stopRequested_ = false;
		size_t inFlightCount_ = 0;
	};
}
