#include "GaussianScene.hpp"

namespace sibr {
    GaussianInstance* GaussianScene::createInstance(const std::string& name, const std::string& assetId, Vector3f p_position, Vector3f p_euler_angle, float p_scale)
    {

        if (instances.find(name) != instances.end()) {
            SIBR_WRG << "Instance with name '" << name << "' already exists." << std::endl;
            return nullptr;
        }

        auto res = instances.emplace(name, std::make_unique<GaussianInstance>(name, assetId, p_position, p_euler_angle, p_scale));

        return res.first->second.get();
    }

    GaussianInstance* GaussianScene::getInstance(const std::string& name)
    {
        return const_cast<GaussianInstance*>(static_cast<const GaussianScene*>(this)->getInstance(name));
    }

    const GaussianInstance* GaussianScene::getInstance(const std::string& name) const
    {
        auto itr = instances.find(name);
        if (itr == instances.end()) {
            return nullptr;
        }

        return itr->second.get();
    }

    bool GaussianScene::removeInstance(const std::string& name)
    {
        auto itr = instances.find(name);
        if (itr == instances.end()) {
            SIBR_WRG << "Cannot remove: Instance '" << name << "' not found." << std::endl;
            return false;
        }

        instances.erase(itr);

        return true;
    }

    size_t GaussianScene::countInstancesUsingAsset(const std::string& assetId) const
    {
        if (assetId.empty()) {
            return 0;
        }

        size_t count = 0;
        for (const auto& pair : instances) {
            if (pair.second && pair.second->getAssetId() == assetId) {
                ++count;
            }
        }

        return count;
    }

    const std::unordered_map<std::string, GaussianInstance::UPtr>& GaussianScene::getInstances() const
    {
        return instances;
    }
}
