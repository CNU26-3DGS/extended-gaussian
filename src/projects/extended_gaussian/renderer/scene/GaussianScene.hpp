#pragma once

# include <core/system/Config.hpp>
#include "Config.hpp"
#include "GaussianInstance.hpp"
#include <core/system/Transform3.hpp>

#include <unordered_map>

namespace sibr {
	class SIBR_EXTENDED_GAUSSIAN_EXPORT GaussianScene {
	public:
		SIBR_CLASS_PTR(GaussianScene);

		GaussianScene() = default;
		
		GaussianScene(const GaussianScene&) = delete;
		GaussianScene& operator=(const GaussianScene&) = delete;

		GaussianInstance* createInstance(const std::string& name, const std::string& assetId = "", Vector3f p_position = Vector3f(), Vector3f p_euler_angle = Vector3f(), float p_scale = 1.f);

		GaussianInstance* getInstance(const std::string& name);
		const GaussianInstance* getInstance(const std::string& name) const;

		bool removeInstance(const std::string& name);
		size_t countInstancesUsingAsset(const std::string& assetId) const;

		const std::unordered_map<std::string, GaussianInstance::UPtr>& getInstances() const;

	private:
		std::unordered_map<std::string, GaussianInstance::UPtr> instances;
	};
}
