#pragma once

#include "ManifestStore.hpp"

#include <boost/filesystem.hpp>
#include <core/system/Config.hpp>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "Config.hpp"
#include "GaussianField.hpp"

namespace sibr {
	enum class CpuState {
		Unloaded,
		Loading,
		Resident,
		EvictQueued,
		Failed
	};

	// Internal per-asset bookkeeping. All fields are protected by ResourceManager::mutex_.
	// Lifetime note: cpu_field is a shared_ptr. Callers that obtain a copy via
	// getCpuFieldShared() keep the field alive even after the record transitions
	// to EvictQueued or Unloaded - the underlying GaussianField is destroyed when
	// the last shared_ptr drops, not when evictCpuNow() runs.
	struct AssetRecord {
		AssetDescriptor desc;
		CpuState cpu_state = CpuState::Unloaded;
		GaussianField::Ptr cpu_field;
		// Frame indices set by markRequested/markVisible; used by eviction policy.
		uint64_t last_requested_frame = 0;
		uint64_t last_visible_frame = 0;
		bool desired_cpu = false;   // Set by SwapManager each tick.
		bool pinned_cpu = false;    // True if manifest pin_cpu OR user pin is active.
		size_t actual_cpu_bytes = 0; // Measured on load; used for totalCpuBytes_ accounting.
	};

	// Thread-safe snapshot of an AssetRecord for external consumers.
	// Returned by snapshotAssets(); safe to read without holding the mutex.
	struct AssetStatus {
		AssetId id;
		CpuState cpu_state = CpuState::Unloaded;
		bool cpu_resident = false;
		bool desired_cpu = false;
		bool pinned_cpu = false;
		size_t actual_cpu_bytes = 0;
		size_t estimated_cpu_bytes = 0;
		int priority = 0;
		boost::filesystem::path model_dir;
	};

	// Manages the CPU-side GaussianField asset registry.
	// All public methods are thread-safe; they acquire mutex_ internally.
	// The sole exception is the private static estimateCpuBytes(), which is
	// stateless and operates only on its argument.
	class SIBR_EXTENDED_GAUSSIAN_EXPORT ResourceManager {
	public:
		SIBR_CLASS_PTR(ResourceManager);

		ResourceManager() = default;

		ResourceManager(const ResourceManager&) = delete;
		ResourceManager& operator=(const ResourceManager&) = delete;

		bool addField(GaussianField::UPtr field);
		size_t registerManifest(const ManifestStore& manifest);
		bool registerAsset(const AssetDescriptor& descriptor);
		bool unregisterAsset(const AssetId& id);
		bool hasAsset(const AssetId& id) const;
		bool getAssetDescriptor(const AssetId& id, AssetDescriptor& descriptor) const;
		GaussianField::Ptr getCpuFieldShared(const AssetId& id) const;

		bool removeField(const std::string& name);
		CpuState cpuState(const AssetId& id) const;
		bool isCpuResident(const AssetId& id) const;
		bool beginCpuLoad(const AssetId& id);
		void completeCpuLoad(const AssetId& id, GaussianField::Ptr field);
		void failCpuLoad(const AssetId& id);
		bool requestCpuEvict(const AssetId& id);
		void evictCpuNow(const AssetId& id);
		void markRequested(const AssetId& id, uint64_t frameIndex);
		void markVisible(const AssetId& id, uint64_t frameIndex);
		void setDesiredCpu(const AssetId& id, bool desired);
		bool setPinnedCpu(const AssetId& id, bool pinned);
		bool isPinnedCpu(const AssetId& id) const;
		size_t totalCpuBytes() const;
		std::vector<AssetId> listAssetIds() const;
		std::vector<AssetStatus> snapshotAssets() const;

	private:
		static size_t estimateCpuBytes(const GaussianField& field);

		mutable std::mutex mutex_;
		std::unordered_map<AssetId, AssetRecord> records_;
		size_t totalCpuBytes_ = 0;
	};
}
