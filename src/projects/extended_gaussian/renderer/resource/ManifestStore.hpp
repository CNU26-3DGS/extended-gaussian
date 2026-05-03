#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"

#include <boost/filesystem.hpp>
#include <core/system/Vector.hpp>

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace sibr {
	using AssetId = std::string;
	using PhaseId = std::string;

	enum class ManifestRuleType {
		Phase,
		CameraBounds,
		Distance
	};

	struct ManifestGlobalSettings {
		size_t target_vram_bytes = 0;
		size_t target_ram_bytes = 0;
		size_t max_upload_bytes_per_frame = 0;
		size_t max_gpu_evictions_per_frame = 2;
		size_t max_cpu_evictions_per_frame = 1;
		int max_concurrent_disk_loads = 1;
		double default_unload_hysteresis_sec = 1.0;
		bool warm_rule_assets_cpu = false;
	};

	struct AssetDescriptor {
		AssetId id;
		boost::filesystem::path model_dir;
		std::vector<std::string> tags;
		Vector3f bounds_min = Vector3f::Zero();
		Vector3f bounds_max = Vector3f::Zero();
		bool has_gaussian_centroid = false;
		Vector3f gaussian_centroid = Vector3f::Zero();
		bool has_focus_bounds = false;
		Vector3f focus_center = Vector3f::Zero();
		Vector3f focus_bounds_min = Vector3f::Zero();
		Vector3f focus_bounds_max = Vector3f::Zero();
		size_t estimated_cpu_bytes = 0;
		size_t estimated_gpu_bytes = 0;
		int priority = 0;
		bool pin_cpu = false;
		bool pin_gpu = false;
		float prefetch_distance = 0.0f;
		float unload_hysteresis_sec = 1.0f;
	};

	struct ManifestRule {
		std::string name;
		ManifestRuleType type = ManifestRuleType::Phase;
		PhaseId phase;
		std::string source = "camera";
		Vector3f region_min = Vector3f::Zero();
		Vector3f region_max = Vector3f::Zero();
		float distance = 0.0f;
		std::vector<AssetId> required;
		std::vector<AssetId> warm;
		std::vector<std::string> required_by_tag;
		std::vector<std::string> warm_by_tag;
	};

	class SIBR_EXTENDED_GAUSSIAN_EXPORT ManifestStore {
	public:
		bool load(const boost::filesystem::path& manifestPath);
		void clear();

		bool empty() const;
		const boost::filesystem::path& path() const;
		const ManifestGlobalSettings& settings() const;
		const std::unordered_map<AssetId, AssetDescriptor>& assets() const;
		const std::vector<ManifestRule>& rules() const;
		const std::unordered_set<AssetId>& referencedAssets() const;
		std::vector<PhaseId> phases() const;

	private:
		boost::filesystem::path manifestPath_;
		ManifestGlobalSettings settings_;
		std::unordered_map<AssetId, AssetDescriptor> assets_;
		std::vector<ManifestRule> rules_;
		std::unordered_set<AssetId> referencedAssets_;
	};
}
