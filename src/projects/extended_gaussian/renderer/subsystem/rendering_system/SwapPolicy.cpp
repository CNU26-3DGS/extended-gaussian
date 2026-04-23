#include "SwapPolicy.hpp"

#include <projects/extended_gaussian/renderer/resource/ResourceManager.hpp>

#include <algorithm>
#include <cmath>

namespace {
	bool phaseMatches(const sibr::ManifestRule& rule, const sibr::PhaseId& currentPhase) {
		return rule.phase.empty() || rule.phase == currentPhase;
	}

	bool containsTag(const std::vector<std::string>& tags, const std::string& tag) {
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	bool pointInBounds(const sibr::Vector3f& point, const sibr::Vector3f& minBounds, const sibr::Vector3f& maxBounds) {
		return point.x() >= minBounds.x() && point.x() <= maxBounds.x()
			&& point.y() >= minBounds.y() && point.y() <= maxBounds.y()
			&& point.z() >= minBounds.z() && point.z() <= maxBounds.z();
	}

	float distanceToBounds(const sibr::Vector3f& point, const sibr::AssetDescriptor& descriptor) {
		const float dx = std::max({ descriptor.bounds_min.x() - point.x(), 0.0f, point.x() - descriptor.bounds_max.x() });
		const float dy = std::max({ descriptor.bounds_min.y() - point.y(), 0.0f, point.y() - descriptor.bounds_max.y() });
		const float dz = std::max({ descriptor.bounds_min.z() - point.z(), 0.0f, point.z() - descriptor.bounds_max.z() });
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	void addAssetsById(std::unordered_set<sibr::AssetId>& destination, const std::vector<sibr::AssetId>& ids) {
		for (const auto& id : ids) {
			if (!id.empty()) {
				destination.insert(id);
			}
		}
	}

	void addAssetsByTag(
		std::unordered_set<sibr::AssetId>& destination,
		const std::unordered_map<sibr::AssetId, sibr::AssetDescriptor>& assets,
		const std::vector<std::string>& tags)
	{
		for (const auto& assetPair : assets) {
			for (const auto& tag : tags) {
				if (containsTag(assetPair.second.tags, tag)) {
					destination.insert(assetPair.first);
					break;
				}
			}
		}
	}
}

namespace sibr {
	PolicyResult SwapPolicy::evaluate(const ViewerContext& context, const ManifestStore& manifest, const ResourceManager& registry) const
	{
		PolicyResult result;
		// registry is accepted for API symmetry with future policy types (e.g. LRU,
		// usage-frequency) that will need asset metadata. Current rule-based evaluation
		// reads only manifest data and ViewerContext.
		(void)registry;

		if (manifest.empty()) {
			return result;
		}

		for (const auto& assetPair : manifest.assets()) {
			const auto& descriptor = assetPair.second;
			if (descriptor.pin_gpu) {
				result.required_gpu.insert(assetPair.first);
				result.protected_assets.insert(assetPair.first);
			}
			if (descriptor.pin_cpu || context.user_pinned_assets.count(assetPair.first) > 0) {
				result.warm_cpu.insert(assetPair.first);
				result.protected_assets.insert(assetPair.first);
			}
		}

		if (manifest.settings().warm_rule_assets_cpu) {
			result.warm_cpu.insert(manifest.referencedAssets().begin(), manifest.referencedAssets().end());
		}

		for (const auto& rule : manifest.rules()) {
			if (!phaseMatches(rule, context.current_phase)) {
				continue;
			}

			switch (rule.type) {
			case ManifestRuleType::Phase:
				addAssetsById(result.required_gpu, rule.required);
				addAssetsById(result.warm_cpu, rule.warm);
				addAssetsByTag(result.required_gpu, manifest.assets(), rule.required_by_tag);
				addAssetsByTag(result.warm_cpu, manifest.assets(), rule.warm_by_tag);
				break;

			case ManifestRuleType::CameraBounds:
				if (pointInBounds(context.camera_pos, rule.region_min, rule.region_max)) {
					addAssetsById(result.required_gpu, rule.required);
					addAssetsById(result.warm_cpu, rule.warm);
					addAssetsByTag(result.required_gpu, manifest.assets(), rule.required_by_tag);
					addAssetsByTag(result.warm_cpu, manifest.assets(), rule.warm_by_tag);
				}
				break;

			case ManifestRuleType::Distance: {
				std::unordered_set<AssetId> requiredCandidates;
				std::unordered_set<AssetId> warmCandidates;
				addAssetsById(requiredCandidates, rule.required);
				addAssetsById(warmCandidates, rule.warm);
				addAssetsByTag(requiredCandidates, manifest.assets(), rule.required_by_tag);
				addAssetsByTag(warmCandidates, manifest.assets(), rule.warm_by_tag);

				for (const auto& assetPair : manifest.assets()) {
					const auto& assetId = assetPair.first;
					const auto& descriptor = assetPair.second;
					const bool isRequiredCandidate = requiredCandidates.count(assetId) > 0;
					const bool isWarmCandidate = warmCandidates.count(assetId) > 0;
					if (!isRequiredCandidate && !isWarmCandidate) {
						continue;
					}

					const float threshold = rule.distance > 0.0f ? rule.distance : descriptor.prefetch_distance;
					if (threshold <= 0.0f) {
						continue;
					}

					if (distanceToBounds(context.camera_pos, descriptor) <= threshold) {
						if (isRequiredCandidate) {
							result.required_gpu.insert(assetId);
						}
						if (isWarmCandidate) {
							result.warm_cpu.insert(assetId);
						}
					}
				}
				break;
			}
			}
		}

		return result;
	}
}
