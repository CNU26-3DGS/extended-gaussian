#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"

#include <projects/extended_gaussian/renderer/resource/AssetLoadWorker.hpp>
#include <projects/extended_gaussian/renderer/resource/ManifestStore.hpp>
#include <projects/extended_gaussian/renderer/resource/ResourceManager.hpp>

#include "SwapPolicy.hpp"
#include "gpu_resource_manager/GPUResourceManager.hpp"

#include <unordered_map>

namespace sibr {
	class SIBR_EXTENDED_GAUSSIAN_EXPORT SwapManager {
	public:
		SIBR_CLASS_PTR(SwapManager);

		struct Stats {
			PhaseId current_phase;
			size_t required_gpu_count = 0;
			size_t warm_cpu_count = 0;
			size_t pending_disk_loads = 0;
			size_t pending_gpu_uploads = 0;
			size_t pending_gpu_evictions = 0;
			size_t cpu_resident_bytes = 0;
			size_t gpu_resident_bytes = 0;
			size_t skipped_instances_last_frame = 0;
			size_t swap_hits = 0;
			size_t swap_misses = 0;
		};

		SwapManager(ResourceManager& registry, GPUResourceManager& gpuManager);
		~SwapManager();

		void setManifest(const ManifestStore* manifest);
		bool hasManifest() const;

		void tick(const ViewerContext& context);
		void setSkippedInstancesLastFrame(size_t skippedInstances);

		const Stats& stats() const;

	private:
		bool isCpuDesired(const AssetId& id, const PolicyResult& result) const;
		bool isGpuDesired(const AssetId& id, const PolicyResult& result) const;
		double hysteresisFor(const AssetId& id) const;

		void updateDesiredStates(const PolicyResult& result);
		void updateEvictableTimers(const PolicyResult& result, const ViewerContext& context);
		void processCompletedCpuLoads();
		void scheduleCpuLoads(const PolicyResult& result);
		void scheduleGpuUploads(const PolicyResult& result);
		size_t evictGpuForBudget(const PolicyResult& result, size_t bytesNeeded);
		size_t evictCpuForBudget(const PolicyResult& result, size_t bytesNeeded);
		void scheduleGpuEvictions(const PolicyResult& result, const ViewerContext& context);
		void scheduleCpuEvictions(const PolicyResult& result, const ViewerContext& context);

		const ManifestStore* manifest_ = nullptr;
		ResourceManager& registry_;
		GPUResourceManager& gpuManager_;
		SwapPolicy policy_;
		AssetLoadWorker worker_;
		std::unordered_map<AssetId, double> evictableSince_;
		Stats stats_;
	};
}
