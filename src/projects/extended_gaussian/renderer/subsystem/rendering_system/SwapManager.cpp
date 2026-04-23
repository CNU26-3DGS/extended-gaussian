#include "SwapManager.hpp"

#include <algorithm>
#include <limits>

namespace {
	const sibr::AssetDescriptor* manifestDescriptor(const sibr::ManifestStore* manifest, const sibr::AssetId& id)
	{
		if (!manifest) {
			return nullptr;
		}

		const auto itr = manifest->assets().find(id);
		if (itr == manifest->assets().end()) {
			return nullptr;
		}

		return &itr->second;
	}

	int assetPriority(const sibr::ManifestStore* manifest, const sibr::AssetId& id)
	{
		const auto* descriptor = manifestDescriptor(manifest, id);
		return descriptor ? descriptor->priority : 0;
	}

}

namespace sibr {
	SwapManager::SwapManager(ResourceManager& registry, GPUResourceManager& gpuManager)
		: registry_(registry), gpuManager_(gpuManager)
	{
	}

	SwapManager::~SwapManager()
	{
		worker_.stop();
	}

	void SwapManager::setManifest(const ManifestStore* manifest)
	{
		manifest_ = manifest;
		evictableSince_.clear();
		worker_.stop();

		if (manifest_ && !manifest_->empty()) {
			worker_.start(manifest_->settings().max_concurrent_disk_loads);
		}
	}

	bool SwapManager::hasManifest() const
	{
		return manifest_ && !manifest_->empty();
	}

	void SwapManager::tick(const ViewerContext& context)
	{
		stats_.current_phase = context.current_phase;
		stats_.pending_gpu_uploads = 0;
		stats_.pending_gpu_evictions = 0;
		stats_.required_gpu_count = 0;
		stats_.warm_cpu_count = 0;

		processCompletedCpuLoads();

		if (!hasManifest()) {
			stats_.pending_disk_loads = worker_.pendingCount() + worker_.inFlightCount();
			stats_.cpu_resident_bytes = registry_.totalCpuBytes();
			stats_.gpu_resident_bytes = gpuManager_.totalBytes();
			return;
		}

		const PolicyResult result = policy_.evaluate(context, *manifest_, registry_);
		stats_.required_gpu_count = result.required_gpu.size();
		stats_.warm_cpu_count = result.warm_cpu.size();

		updateDesiredStates(result);
		updateEvictableTimers(result, context);
		scheduleCpuLoads(result);
		scheduleGpuUploads(result);
		scheduleGpuEvictions(result, context);
		scheduleCpuEvictions(result, context);

		stats_.pending_disk_loads = worker_.pendingCount() + worker_.inFlightCount();
		stats_.cpu_resident_bytes = registry_.totalCpuBytes();
		stats_.gpu_resident_bytes = gpuManager_.totalBytes();
	}

	void SwapManager::setSkippedInstancesLastFrame(size_t skippedInstances)
	{
		stats_.skipped_instances_last_frame = skippedInstances;
	}

	const SwapManager::Stats& SwapManager::stats() const
	{
		return stats_;
	}

	bool SwapManager::isCpuDesired(const AssetId& id, const PolicyResult& result) const
	{
		return result.required_gpu.count(id) > 0
			|| result.warm_cpu.count(id) > 0
			|| result.protected_assets.count(id) > 0;
	}

	bool SwapManager::isGpuDesired(const AssetId& id, const PolicyResult& result) const
	{
		return result.required_gpu.count(id) > 0 || result.protected_assets.count(id) > 0;
	}

	double SwapManager::hysteresisFor(const AssetId& id) const
	{
		if (!manifest_) {
			return 1.0;
		}

		const AssetDescriptor* descriptor = manifestDescriptor(manifest_, id);
		if (descriptor && descriptor->unload_hysteresis_sec > 0.0f) {
			return descriptor->unload_hysteresis_sec;
		}

		return std::max(manifest_->settings().default_unload_hysteresis_sec, 0.0);
	}

	void SwapManager::updateDesiredStates(const PolicyResult& result)
	{
		for (const auto& assetPair : manifest_->assets()) {
			registry_.setDesiredCpu(assetPair.first, isCpuDesired(assetPair.first, result));
		}
	}

	void SwapManager::updateEvictableTimers(const PolicyResult& result, const ViewerContext& context)
	{
		for (const auto& assetPair : manifest_->assets()) {
			const auto& id = assetPair.first;
			if (isCpuDesired(id, result) || isGpuDesired(id, result)) {
				evictableSince_.erase(id);
				continue;
			}
			evictableSince_.try_emplace(id, context.app_time_sec);
		}
	}

	void SwapManager::processCompletedCpuLoads()
	{
		for (auto& result : worker_.drainCompleted()) {
			if (result.field) {
				registry_.completeCpuLoad(result.id, std::move(result.field));
			}
			else {
				SIBR_WRG << result.error << std::endl;
				registry_.failCpuLoad(result.id);
			}
		}
	}

	void SwapManager::scheduleCpuLoads(const PolicyResult& result)
	{
		if (!manifest_) {
			return;
		}

		size_t residentBytes = registry_.totalCpuBytes();
		const size_t targetBytes = manifest_->settings().target_ram_bytes;

		for (const auto& assetPair : manifest_->assets()) {
			const auto& id = assetPair.first;
			if (!isCpuDesired(id, result)) {
				continue;
			}
			if (registry_.isCpuResident(id) || registry_.cpuState(id) == CpuState::Loading) {
				continue;
			}

			const size_t estimatedBytes = assetPair.second.estimated_cpu_bytes;
			if (targetBytes > 0 && estimatedBytes > 0 && residentBytes + estimatedBytes > targetBytes) {
				const size_t bytesNeeded = residentBytes + estimatedBytes - targetBytes;
				residentBytes -= std::min(residentBytes, evictCpuForBudget(result, bytesNeeded));
			}
			if (targetBytes > 0 && estimatedBytes > 0 && residentBytes + estimatedBytes > targetBytes) {
				continue;
			}
			if (!registry_.beginCpuLoad(id)) {
				continue;
			}
			if (!worker_.enqueue(assetPair.second)) {
				registry_.failCpuLoad(id);
			}
		}
	}

	void SwapManager::scheduleGpuUploads(const PolicyResult& result)
	{
		std::vector<AssetId> uploadOrder(result.required_gpu.begin(), result.required_gpu.end());
		std::sort(uploadOrder.begin(), uploadOrder.end(), [this](const AssetId& lhs, const AssetId& rhs) {
			const int lhsPriority = assetPriority(manifest_, lhs);
			const int rhsPriority = assetPriority(manifest_, rhs);
			if (lhsPriority != rhsPriority) {
				return lhsPriority > rhsPriority;
			}
			return lhs < rhs;
		});

		size_t uploadedBytes = 0;
		const size_t uploadBudget = manifest_ ? manifest_->settings().max_upload_bytes_per_frame : 0;
		size_t residentBytes = gpuManager_.totalBytes();
		const size_t targetBytes = manifest_ ? manifest_->settings().target_vram_bytes : 0;

		for (const auto& id : uploadOrder) {
			if (gpuManager_.has(id)) {
				++stats_.swap_hits;
				continue;
			}

			const auto cpuField = registry_.getCpuFieldShared(id);
			if (!cpuField) {
				++stats_.swap_misses;
				continue;
			}

			const AssetDescriptor* descriptor = manifestDescriptor(manifest_, id);
			const size_t estimatedBytes = descriptor ? descriptor->estimated_gpu_bytes : 0;
			// If size is unknown (0), skip per-frame budget enforcement but still
			// allow the upload. This is safe only for small manifests; if assets
			// lack estimated_gpu_bytes entries, VRAM pressure is uncontrolled.
			const bool sizeKnown = estimatedBytes > 0;

			if (sizeKnown && uploadBudget > 0 && uploadedBytes + estimatedBytes > uploadBudget) {
				++stats_.pending_gpu_uploads;
				continue;
			}
			if (sizeKnown && targetBytes > 0 && residentBytes + estimatedBytes > targetBytes) {
				const size_t bytesNeeded = residentBytes + estimatedBytes - targetBytes;
				residentBytes -= std::min(residentBytes, evictGpuForBudget(result, bytesNeeded));
			}
			if (sizeKnown && targetBytes > 0 && residentBytes + estimatedBytes > targetBytes) {
				++stats_.pending_gpu_uploads;
				continue;
			}

			if (!gpuManager_.beginUpload(id)) {
				continue;
			}

			auto gpuField = std::make_shared<GPUGaussianField>(id, cpuField.get());
			gpuManager_.completeUpload(id, gpuField);
			uploadedBytes += gpuField->bytes;
			residentBytes += gpuField->bytes;
			++stats_.swap_hits;
		}
	}

	size_t SwapManager::evictGpuForBudget(const PolicyResult& result, size_t bytesNeeded)
	{
		if (bytesNeeded == 0) {
			return 0;
		}

		std::vector<GPUResourceManager::GpuAssetStatus> candidates;
		for (const auto& status : gpuManager_.snapshot()) {
			if (!status.resident) {
				continue;
			}
			if (isGpuDesired(status.id, result) || result.protected_assets.count(status.id) > 0) {
				continue;
			}
			candidates.emplace_back(status);
		}

		std::sort(candidates.begin(), candidates.end(), [this](const GPUResourceManager::GpuAssetStatus& lhs, const GPUResourceManager::GpuAssetStatus& rhs) {
			const int lhsPriority = assetPriority(manifest_, lhs.id);
			const int rhsPriority = assetPriority(manifest_, rhs.id);
			if (lhsPriority != rhsPriority) {
				return lhsPriority < rhsPriority;
			}
			return lhs.actual_gpu_bytes > rhs.actual_gpu_bytes;
		});

		const size_t budgetSetting = manifest_ ? manifest_->settings().max_gpu_evictions_per_frame : 0;
		size_t remainingBudget = (budgetSetting == 0) ? std::numeric_limits<size_t>::max() : budgetSetting;

		size_t freedBytes = 0;
		for (const auto& candidate : candidates) {
			if (freedBytes >= bytesNeeded || remainingBudget == 0) {
				break;
			}
			if (!gpuManager_.requestEvict(candidate.id)) {
				continue;
			}
			gpuManager_.evictNow(candidate.id);
			freedBytes += candidate.actual_gpu_bytes;
			--remainingBudget;
			++stats_.pending_gpu_evictions;
		}
		return freedBytes;
	}

	void SwapManager::scheduleGpuEvictions(const PolicyResult& result, const ViewerContext& context)
	{
		std::vector<GPUResourceManager::GpuAssetStatus> candidates;
		const auto gpuSnapshot = gpuManager_.snapshot();
		for (const auto& status : gpuSnapshot) {
			if (!status.resident) {
				continue;
			}
			if (isGpuDesired(status.id, result) || result.protected_assets.count(status.id) > 0) {
				continue;
			}
			const auto timerIt = evictableSince_.find(status.id);
			if (timerIt == evictableSince_.end()) {
				continue;
			}
			if (context.app_time_sec - timerIt->second < hysteresisFor(status.id)) {
				continue;
			}
			candidates.emplace_back(status);
		}

		std::sort(candidates.begin(), candidates.end(), [this](const auto& lhs, const auto& rhs) {
			const int lhsPriority = assetPriority(manifest_, lhs.id);
			const int rhsPriority = assetPriority(manifest_, rhs.id);
			if (lhsPriority != rhsPriority) {
				return lhsPriority < rhsPriority;
			}
			return lhs.actual_gpu_bytes > rhs.actual_gpu_bytes;
		});

		const size_t budgetSetting = manifest_ ? manifest_->settings().max_gpu_evictions_per_frame : 0;
		// 0 means no per-frame limit; use SIZE_MAX as the sentinel to avoid
		// re-computing candidates.size() in the loop condition.
		size_t remainingBudget = (budgetSetting == 0) ? std::numeric_limits<size_t>::max() : budgetSetting;

		for (const auto& candidate : candidates) {
			if (remainingBudget == 0) {
				break;
			}
			if (!gpuManager_.requestEvict(candidate.id)) {
				continue;
			}
			gpuManager_.evictNow(candidate.id);
			--remainingBudget;
			++stats_.pending_gpu_evictions;
		}
	}

	void SwapManager::scheduleCpuEvictions(const PolicyResult& result, const ViewerContext& context)
	{
		auto assets = registry_.snapshotAssets();
		std::sort(assets.begin(), assets.end(), [](const auto& lhs, const auto& rhs) {
			if (lhs.priority != rhs.priority) {
				return lhs.priority < rhs.priority;
			}
			return lhs.actual_cpu_bytes > rhs.actual_cpu_bytes;
		});

		const size_t cpuBudgetSetting = manifest_ ? manifest_->settings().max_cpu_evictions_per_frame : 0;
		size_t remainingBudget = (cpuBudgetSetting == 0) ? std::numeric_limits<size_t>::max() : cpuBudgetSetting;

		for (const auto& asset : assets) {
			if (remainingBudget == 0) {
				break;
			}
			if (!asset.cpu_resident || isCpuDesired(asset.id, result) || asset.pinned_cpu) {
				continue;
			}
			if (gpuManager_.has(asset.id)) {
				continue;
			}
			const auto timerIt = evictableSince_.find(asset.id);
			if (timerIt == evictableSince_.end()) {
				continue;
			}
			if (context.app_time_sec - timerIt->second < hysteresisFor(asset.id)) {
				continue;
			}
			if (!registry_.requestCpuEvict(asset.id)) {
				continue;
			}
			registry_.evictCpuNow(asset.id);
			--remainingBudget;
		}
	}

	size_t SwapManager::evictCpuForBudget(const PolicyResult& result, size_t bytesNeeded)
	{
		if (bytesNeeded == 0) {
			return 0;
		}

		auto assets = registry_.snapshotAssets();
		std::sort(assets.begin(), assets.end(), [](const AssetStatus& lhs, const AssetStatus& rhs) {
			if (lhs.priority != rhs.priority) {
				return lhs.priority < rhs.priority;
			}
			return lhs.actual_cpu_bytes > rhs.actual_cpu_bytes;
		});

		size_t freedBytes = 0;
		for (const auto& asset : assets) {
			if (freedBytes >= bytesNeeded) {
				break;
			}
			if (!asset.cpu_resident || isCpuDesired(asset.id, result) || asset.pinned_cpu) {
				continue;
			}
			if (gpuManager_.has(asset.id)) {
				continue;
			}
			if (!registry_.requestCpuEvict(asset.id)) {
				continue;
			}
			registry_.evictCpuNow(asset.id);
			freedBytes += asset.actual_cpu_bytes;
		}
		return freedBytes;
	}
}
