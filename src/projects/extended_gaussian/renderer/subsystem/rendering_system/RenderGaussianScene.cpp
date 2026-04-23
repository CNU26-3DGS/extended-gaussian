#include "RenderGaussianScene.hpp"

#include <projects/extended_gaussian/renderer/scene/GaussianScene.hpp>

namespace sibr {
	bool RenderGaussianScene::createInstance(const GaussianInstance* originInstance)
	{
		if (!originInstance)
		{
			return false;
		}

		if (instances.find(originInstance) != instances.end()) {
			SIBR_WRG << "RenderGaussianInstance for this GaussianInstance already exists." << std::endl;
			return false;
		}

		auto renderInstance = std::make_unique<RenderGaussianInstance>(originInstance);

		instances.emplace(originInstance, std::move(renderInstance));
		return true;
	}

	const RenderGaussianInstance* RenderGaussianScene::getInstance(const GaussianInstance* originInstance) const
	{
		if (!originInstance)
		{
			return nullptr;
		}
		auto itr = instances.find(originInstance);
		if (itr == instances.end()) {
			return nullptr;
		}
		return itr->second.get();
	}

	RenderGaussianInstance* RenderGaussianScene::getInstance(const GaussianInstance* originInstance)
	{
		if (!originInstance)
		{
			return nullptr;
		}
		return const_cast<RenderGaussianInstance*>(
			static_cast<const RenderGaussianScene*>(this)->getInstance(originInstance)
			);
	}

	bool RenderGaussianScene::removeInstance(const GaussianInstance* originInstance)
	{
		if (!originInstance)
		{
			return false;
		}
		auto itr = instances.find(originInstance);
		if (itr == instances.end()) {
			SIBR_WRG << "Cannot remove: RenderGaussianInstance not found for the given origin." << std::endl;
			return false;
		}

		instances.erase(itr);
		return true;
	}

	const std::unordered_map<const GaussianInstance*, RenderGaussianInstance::UPtr>& RenderGaussianScene::getInstances() const {
		return instances;
	}
}