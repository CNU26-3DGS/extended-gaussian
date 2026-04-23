#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"
#include "gpu_resource_manager/GPUGaussianField.hpp"

namespace sibr {
	class GaussianInstance;
	class GaussianField;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT RenderGaussianInstance {
	public:
		SIBR_CLASS_PTR(RenderGaussianInstance);

		RenderGaussianInstance(const GaussianInstance* p_origin);

		~RenderGaussianInstance() = default;

		const GaussianInstance* getOrigin() const;
		const std::string& getAssetId() const;

		const GPUGaussianField* getGPUField() const;
		void setAssetId(const std::string& p_assetId, const GaussianField* cpu_field = nullptr);

	private:
		const GaussianInstance* origin;
		std::string assetId;
	};
}
