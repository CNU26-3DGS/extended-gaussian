#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"
#include "RenderGaussianInstance.hpp"

#include <unordered_map>

namespace sibr {
	class GaussianInstance;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT RenderGaussianScene {
	public:
		SIBR_CLASS_PTR(RenderGaussianScene);

		RenderGaussianScene() = default;

		RenderGaussianScene(const RenderGaussianScene&) = delete;
		RenderGaussianScene& operator=(const RenderGaussianScene&) = delete;

		bool createInstance(const GaussianInstance* origin);

		RenderGaussianInstance* getInstance(const GaussianInstance* origin);
		const RenderGaussianInstance* getInstance(const GaussianInstance* origin) const;

		bool removeInstance(const GaussianInstance* origin);

		const std::unordered_map<const GaussianInstance*, RenderGaussianInstance::UPtr>& getInstances() const;

	private:
		std::unordered_map<const GaussianInstance*, RenderGaussianInstance::UPtr> instances;
	};
}