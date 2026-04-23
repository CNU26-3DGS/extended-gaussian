#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"
#include "GPUGaussianField.hpp"
#include <projects/extended_gaussian/renderer/resource/ManifestStore.hpp>

#include <mutex>
#include <unordered_map>
#include <vector>

namespace sibr {
	class GaussianField;
	enum class GpuState {
		Unloaded,
		UploadQueued,
		Resident,
		EvictQueued,
		Failed
	};

	class SIBR_EXTENDED_GAUSSIAN_EXPORT GPUResourceManager {
	public:
		SIBR_CLASS_PTR(GPUResourceManager);

		static GPUResourceManager& getInstance();

		GPUResourceManager(const GPUResourceManager&) = delete;
		GPUResourceManager& operator=(const GPUResourceManager&) = delete;

		struct GpuAssetStatus {
			AssetId id;
			GpuState state = GpuState::Unloaded;
			size_t actual_gpu_bytes = 0;
			bool resident = false;
		};

		bool has(const AssetId& assetId) const;
		GpuState state(const AssetId& assetId) const;
		bool addField(const std::string& assetId, const GaussianField* field);
		bool beginUpload(const AssetId& assetId);
		void completeUpload(const AssetId& assetId, GPUGaussianField::Ptr field);
		void failUpload(const AssetId& assetId);

		std::shared_ptr<const GPUGaussianField> getField(const std::string& assetId) const;
		GPUGaussianField::Ptr getField(const std::string& assetId);

		bool removeField(const std::string& assetId);
		bool requestEvict(const AssetId& assetId);
		void evictNow(const AssetId& assetId);
		size_t totalBytes() const;
		size_t gpuBytes(const AssetId& assetId) const;
		std::vector<GpuAssetStatus> snapshot() const;

		const std::unordered_map<std::string, GPUGaussianField::Ptr> getFields() const;

		int CleanUp();

	private:
		struct GpuAssetRecord {
			GpuState state = GpuState::Unloaded;
			GPUGaussianField::Ptr field;
			size_t actual_gpu_bytes = 0;
		};

		GPUResourceManager() = default;
		mutable std::mutex mutex_;
		std::unordered_map<std::string, GpuAssetRecord> gpu_fields;
		size_t totalBytes_ = 0;
	};
}
