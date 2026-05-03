#include "ResourceManager.hpp"

#include <algorithm>

namespace sibr {
	size_t ResourceManager::estimateCpuBytes(const GaussianField& field)
	{
		size_t total = sizeof(GaussianField);
		total += field.pos.size() * sizeof(Vector3f);
		total += field.scale.size() * sizeof(Vector3f);
		total += field.rot.size() * sizeof(Vector4f);
		total += field.opacities.size() * sizeof(float);
		total += field.SHs.size() * sizeof(float);
		return total;
	}

	bool ResourceManager::addField(GaussianField::UPtr field)
	{
		if (!field) {
			return false;
		}

		GaussianField::Ptr sharedField(field.release());
		const AssetId assetId = sharedField->name;
		if (assetId.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		const auto existing = records_.find(assetId);
		if (existing != records_.end()
			&& existing->second.cpu_state == CpuState::Resident
			&& existing->second.cpu_field) {
			SIBR_WRG << "GaussianField with asset id '" << assetId << "' already exists." << std::endl;
			return false;
		}

		auto& record = records_[assetId];
		record.desc.id = assetId;
		record.desc.model_dir = sharedField->path;
		record.desc.bounds_min = sharedField->min_edges;
		record.desc.bounds_max = sharedField->max_edges;
		record.desc.has_gaussian_centroid = sharedField->has_centroid;
		record.desc.gaussian_centroid = sharedField->centroid;
		record.desc.has_focus_bounds = sharedField->has_focus_bounds;
		record.desc.focus_center = sharedField->focus_center;
		record.desc.focus_bounds_min = sharedField->focus_bounds_min;
		record.desc.focus_bounds_max = sharedField->focus_bounds_max;

		const size_t estimatedBytes = estimateCpuBytes(*sharedField);
		record.desc.estimated_cpu_bytes = estimatedBytes;
		record.pinned_cpu = record.pinned_cpu || record.desc.pin_cpu;

		totalCpuBytes_ -= record.actual_cpu_bytes;
		record.actual_cpu_bytes = estimatedBytes;
		totalCpuBytes_ += record.actual_cpu_bytes;
		record.cpu_field = std::move(sharedField);
		record.cpu_state = CpuState::Resident;
		return true;
	}

	size_t ResourceManager::registerManifest(const ManifestStore& manifest)
	{
		size_t registeredCount = 0;
		for (const auto& assetPair : manifest.assets()) {
			if (registerAsset(assetPair.second)) {
				++registeredCount;
			}
		}
		return registeredCount;
	}

	bool ResourceManager::registerAsset(const AssetDescriptor& descriptor)
	{
		if (descriptor.id.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		auto& record = records_[descriptor.id];
		record.desc = descriptor;
		record.pinned_cpu = descriptor.pin_cpu || record.pinned_cpu;
		return true;
	}

	bool ResourceManager::unregisterAsset(const AssetId& id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end()) {
			return false;
		}
		totalCpuBytes_ -= itr->second.actual_cpu_bytes;
		records_.erase(itr);
		return true;
	}

	bool ResourceManager::hasAsset(const AssetId& id) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return records_.find(id) != records_.end();
	}

	bool ResourceManager::getAssetDescriptor(const AssetId& id, AssetDescriptor& descriptor) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end()) {
			return false;
		}
		descriptor = itr->second.desc;
		return true;
	}

	GaussianField::Ptr ResourceManager::getCpuFieldShared(const AssetId& id) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end()) {
			return nullptr;
		}
		return itr->second.cpu_field;
	}

	bool ResourceManager::removeField(const std::string& name)
	{
		return unregisterAsset(name);
	}

	CpuState ResourceManager::cpuState(const AssetId& id) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end()) {
			return CpuState::Unloaded;
		}
		return itr->second.cpu_state;
	}

	bool ResourceManager::isCpuResident(const AssetId& id) const
	{
		return cpuState(id) == CpuState::Resident;
	}

	bool ResourceManager::beginCpuLoad(const AssetId& id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end()
			|| itr->second.cpu_state == CpuState::Loading
			|| itr->second.cpu_state == CpuState::Resident
			|| itr->second.cpu_state == CpuState::EvictQueued) {
			return false;
		}
		itr->second.cpu_state = CpuState::Loading;
		return true;
	}

	void ResourceManager::completeCpuLoad(const AssetId& id, GaussianField::Ptr field)
	{
		if (!field) {
			failCpuLoad(id);
			return;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		auto& record = records_[id];
		record.desc.id = id;
		record.desc.model_dir = field->path;
		record.desc.bounds_min = field->min_edges;
		record.desc.bounds_max = field->max_edges;
		record.desc.has_gaussian_centroid = field->has_centroid;
		record.desc.gaussian_centroid = field->centroid;
		record.desc.has_focus_bounds = field->has_focus_bounds;
		record.desc.focus_center = field->focus_center;
		record.desc.focus_bounds_min = field->focus_bounds_min;
		record.desc.focus_bounds_max = field->focus_bounds_max;
		record.desc.estimated_cpu_bytes = estimateCpuBytes(*field);
		totalCpuBytes_ -= record.actual_cpu_bytes;
		record.actual_cpu_bytes = estimateCpuBytes(*field);
		totalCpuBytes_ += record.actual_cpu_bytes;
		record.cpu_field = std::move(field);
		record.cpu_state = CpuState::Resident;
	}

	void ResourceManager::failCpuLoad(const AssetId& id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto& record = records_[id];
		record.desc.id = id;
		record.cpu_state = CpuState::Failed;
	}

	bool ResourceManager::requestCpuEvict(const AssetId& id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end() || itr->second.pinned_cpu || itr->second.cpu_state != CpuState::Resident) {
			return false;
		}
		itr->second.cpu_state = CpuState::EvictQueued;
		return true;
	}

	void ResourceManager::evictCpuNow(const AssetId& id)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr == records_.end() || itr->second.cpu_state != CpuState::EvictQueued) {
			return;
		}
		totalCpuBytes_ -= itr->second.actual_cpu_bytes;
		itr->second.actual_cpu_bytes = 0;
		itr->second.cpu_field.reset();
		itr->second.cpu_state = CpuState::Unloaded;
	}

	void ResourceManager::markRequested(const AssetId& id, uint64_t frameIndex)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr != records_.end()) {
			itr->second.last_requested_frame = frameIndex;
		}
	}

	void ResourceManager::markVisible(const AssetId& id, uint64_t frameIndex)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr != records_.end()) {
			itr->second.last_visible_frame = frameIndex;
		}
	}

	void ResourceManager::setDesiredCpu(const AssetId& id, bool desired)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr != records_.end()) {
			itr->second.desired_cpu = desired;
		}
	}

	bool ResourceManager::setPinnedCpu(const AssetId& id, bool pinned)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		if (itr != records_.end()) {
			itr->second.pinned_cpu = pinned;
			return true;
		}
		return false;
	}

	bool ResourceManager::isPinnedCpu(const AssetId& id) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto itr = records_.find(id);
		return itr != records_.end() && itr->second.pinned_cpu;
	}

	size_t ResourceManager::totalCpuBytes() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return totalCpuBytes_;
	}

	std::vector<AssetId> ResourceManager::listAssetIds() const
	{
		std::vector<AssetId> ids;
		std::lock_guard<std::mutex> lock(mutex_);
		ids.reserve(records_.size());
		for (const auto& recordPair : records_) {
			ids.emplace_back(recordPair.first);
		}
		std::sort(ids.begin(), ids.end());
		return ids;
	}

	std::vector<AssetStatus> ResourceManager::snapshotAssets() const
	{
		std::vector<AssetStatus> assets;
		std::lock_guard<std::mutex> lock(mutex_);
		assets.reserve(records_.size());
		for (const auto& recordPair : records_) {
			const auto& record = recordPair.second;
			assets.push_back(AssetStatus{
				recordPair.first,
				record.cpu_state,
				record.cpu_state == CpuState::Resident,
				record.desired_cpu,
				record.pinned_cpu,
				record.actual_cpu_bytes,
				record.desc.estimated_cpu_bytes,
				record.desc.priority,
				record.desc.model_dir
				});
		}
		std::sort(assets.begin(), assets.end(), [](const AssetStatus& lhs, const AssetStatus& rhs) {
			return lhs.id < rhs.id;
		});
		return assets;
	}
}
