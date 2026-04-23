#include "RenderGaussianInstance.hpp"

#include <projects/extended_gaussian/renderer/scene/GaussianInstance.hpp>
#include "gpu_resource_manager/GPUResourceManager.hpp"
#include "gpu_resource_manager/GPUGaussianField.hpp"

namespace sibr {
	RenderGaussianInstance::RenderGaussianInstance(const GaussianInstance* p_origin)
		: origin(p_origin)
	{
		if (origin) {
			assetId = origin->getAssetId();
		}
	}

	const GaussianInstance* RenderGaussianInstance::getOrigin() const
	{
		return origin;
	}

	const std::string& RenderGaussianInstance::getAssetId() const
	{
		return assetId;
	}

	const GPUGaussianField* RenderGaussianInstance::getGPUField() const
	{
		if (assetId.empty()) {
			return nullptr;
		}

		return GPUResourceManager::getInstance().getField(assetId).get();
	}

	void RenderGaussianInstance::setAssetId(const std::string& p_assetId, const GaussianField* cpu_field)
	{
		assetId = p_assetId;
		(void)cpu_field;
	}
}
