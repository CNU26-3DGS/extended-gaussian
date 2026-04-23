#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"

#include <projects/extended_gaussian/renderer/resource/ManifestStore.hpp>

#include <unordered_set>

namespace sibr {
	class ResourceManager;

	struct ViewerContext {
		Vector3f camera_pos = Vector3f::Zero();
		Vector3f camera_forward = Vector3f(0.0f, 0.0f, -1.0f);
		Vector3f camera_up = Vector3f(0.0f, 1.0f, 0.0f);
		PhaseId current_phase;
		double app_time_sec = 0.0;
		double dt_sec = 0.0;
		uint64_t frame_index = 0;
		std::unordered_set<AssetId> user_pinned_assets;
	};

	struct PolicyResult {
		// Assets that must be GPU-resident this frame for rendering.
		std::unordered_set<AssetId> required_gpu;
		// Assets that should be CPU-resident for fast future upload (prefetch).
		std::unordered_set<AssetId> warm_cpu;
		// Assets that must not be evicted this frame regardless of memory pressure.
		// Superset: includes required_gpu assets and pin_cpu/pin_gpu assets.
		std::unordered_set<AssetId> protected_assets;
	};

	class SIBR_EXTENDED_GAUSSIAN_EXPORT SwapPolicy {
	public:
		PolicyResult evaluate(const ViewerContext& context, const ManifestStore& manifest, const ResourceManager& registry) const;
	};
}
