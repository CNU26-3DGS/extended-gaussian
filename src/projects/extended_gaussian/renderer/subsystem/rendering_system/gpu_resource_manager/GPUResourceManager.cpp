#include "GPUResourceManager.hpp"
#include <projects/extended_gaussian/renderer/resource/GaussianField.hpp>

#include <algorithm>

namespace sibr {
    GPUResourceManager& GPUResourceManager::getInstance()
    {
        static GPUResourceManager resource_manager;
        return resource_manager;
    }

    bool GPUResourceManager::has(const AssetId& assetId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto itr = gpu_fields.find(assetId);
        return itr != gpu_fields.end() && itr->second.state == GpuState::Resident && itr->second.field != nullptr;
    }

    GpuState GPUResourceManager::state(const AssetId& assetId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto itr = gpu_fields.find(assetId);
        if (itr == gpu_fields.end()) {
            return GpuState::Unloaded;
        }
        return itr->second.state;
    }

    bool GPUResourceManager::addField(const std::string& assetId, const GaussianField* field)
    {
        if (!field || assetId.empty()) {
            return false;
        }
        if (!beginUpload(assetId)) {
            return false;
        }

        completeUpload(assetId, std::make_shared<GPUGaussianField>(assetId, field));
        return true;
    }

    bool GPUResourceManager::beginUpload(const AssetId& assetId)
    {
        if (assetId.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto& record = gpu_fields[assetId];
        if (record.state == GpuState::UploadQueued || record.state == GpuState::Resident) {
            return false;
        }
        record.state = GpuState::UploadQueued;
        return true;
    }

    void GPUResourceManager::completeUpload(const AssetId& assetId, GPUGaussianField::Ptr field)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& record = gpu_fields[assetId];
        totalBytes_ -= record.actual_gpu_bytes;
        record.field = std::move(field);
        record.actual_gpu_bytes = record.field ? record.field->bytes : 0;
        totalBytes_ += record.actual_gpu_bytes;
        record.state = record.field ? GpuState::Resident : GpuState::Failed;
    }

    void GPUResourceManager::failUpload(const AssetId& assetId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& record = gpu_fields[assetId];
        record.state = GpuState::Failed;
    }

    std::shared_ptr<const GPUGaussianField> GPUResourceManager::getField(const std::string& assetId) const
    {
        if (assetId.empty()) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto itr = gpu_fields.find(assetId);
        if (itr == gpu_fields.end() || itr->second.state != GpuState::Resident) {
            return nullptr;
        }
        return itr->second.field;
    }

    GPUGaussianField::Ptr GPUResourceManager::getField(const std::string& assetId)
    {
        if (assetId.empty()) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto itr = gpu_fields.find(assetId);
        if (itr == gpu_fields.end() || itr->second.state != GpuState::Resident) {
            return nullptr;
        }
        return itr->second.field;
    }

    bool GPUResourceManager::removeField(const std::string& assetId)
    {
        if (!requestEvict(assetId)) {
            return false;
        }
        evictNow(assetId);
        return true;
    }

    bool GPUResourceManager::requestEvict(const AssetId& assetId)
    {
        if (assetId.empty()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const auto itr = gpu_fields.find(assetId);
        if (itr == gpu_fields.end() || itr->second.state != GpuState::Resident) {
            return false;
        }
        itr->second.state = GpuState::EvictQueued;
        return true;
    }

    void GPUResourceManager::evictNow(const AssetId& assetId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto itr = gpu_fields.find(assetId);
        if (itr == gpu_fields.end()) {
            return;
        }
        totalBytes_ -= itr->second.actual_gpu_bytes;
        itr->second.actual_gpu_bytes = 0;
        itr->second.field.reset();
        itr->second.state = GpuState::Unloaded;
    }

    size_t GPUResourceManager::totalBytes() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return totalBytes_;
    }

    size_t GPUResourceManager::gpuBytes(const AssetId& assetId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto itr = gpu_fields.find(assetId);
        if (itr == gpu_fields.end()) {
            return 0;
        }
        return itr->second.actual_gpu_bytes;
    }

    std::vector<GPUResourceManager::GpuAssetStatus> GPUResourceManager::snapshot() const
    {
        std::vector<GpuAssetStatus> snapshot;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.reserve(gpu_fields.size());
        for (const auto& gpuPair : gpu_fields) {
            snapshot.push_back(GpuAssetStatus{
                gpuPair.first,
                gpuPair.second.state,
                gpuPair.second.actual_gpu_bytes,
                gpuPair.second.state == GpuState::Resident && gpuPair.second.field != nullptr
                });
        }
        std::sort(snapshot.begin(), snapshot.end(), [](const GpuAssetStatus& lhs, const GpuAssetStatus& rhs) {
            return lhs.id < rhs.id;
        });
        return snapshot;
    }

    int GPUResourceManager::CleanUp()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        totalBytes_ = 0;
        for (auto& gpuPair : gpu_fields) {
            gpuPair.second.actual_gpu_bytes = 0;
            gpuPair.second.field.reset();
            gpuPair.second.state = GpuState::Unloaded;
        }
        return 0;
    }

    const std::unordered_map<std::string, GPUGaussianField::Ptr> GPUResourceManager::getFields() const
    {
        std::unordered_map<std::string, GPUGaussianField::Ptr> fields;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& gpuPair : gpu_fields) {
            if (gpuPair.second.field) {
                fields.emplace(gpuPair.first, gpuPair.second.field);
            }
        }
        return fields;
    }
}
