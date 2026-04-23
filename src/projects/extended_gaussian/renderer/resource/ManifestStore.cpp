#include "ManifestStore.hpp"

#include "picojson/picojson.hpp"

#include <boost/filesystem.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace {
	bool parseBool(const picojson::object& object, const std::string& key, bool defaultValue) {
		const auto it = object.find(key);
		if (it == object.end()) {
			return defaultValue;
		}
		if (!it->second.is<bool>()) {
			return defaultValue;
		}
		return it->second.get<bool>();
	}

	double parseDouble(const picojson::object& object, const std::string& key, double defaultValue) {
		const auto it = object.find(key);
		if (it == object.end()) {
			return defaultValue;
		}
		if (!it->second.is<double>()) {
			return defaultValue;
		}
		return it->second.get<double>();
	}

	int parseInt(const picojson::object& object, const std::string& key, int defaultValue) {
		return static_cast<int>(parseDouble(object, key, static_cast<double>(defaultValue)));
	}

	size_t parseSize(const picojson::object& object, const std::string& key, size_t defaultValue) {
		return static_cast<size_t>(parseDouble(object, key, static_cast<double>(defaultValue)));
	}

	std::string parseString(const picojson::object& object, const std::string& key, const std::string& defaultValue = std::string()) {
		const auto it = object.find(key);
		if (it == object.end()) {
			return defaultValue;
		}
		if (!it->second.is<std::string>()) {
			return defaultValue;
		}
		return it->second.get<std::string>();
	}

	std::vector<std::string> parseStringArray(const picojson::object& object, const std::string& key) {
		std::vector<std::string> values;
		const auto it = object.find(key);
		if (it == object.end() || !it->second.is<picojson::array>()) {
			return values;
		}

		for (const auto& value : it->second.get<picojson::array>()) {
			if (value.is<std::string>()) {
				values.emplace_back(value.get<std::string>());
			}
		}
		return values;
	}

	bool containsTag(const std::vector<std::string>& tags, const std::string& tag) {
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	sibr::Vector3f parseVector3(const picojson::object& object, const std::string& key, const sibr::Vector3f& defaultValue) {
		const auto it = object.find(key);
		if (it == object.end() || !it->second.is<picojson::array>()) {
			return defaultValue;
		}

		const auto& values = it->second.get<picojson::array>();
		if (values.size() != 3 || !values[0].is<double>() || !values[1].is<double>() || !values[2].is<double>()) {
			return defaultValue;
		}

		return sibr::Vector3f(
			static_cast<float>(values[0].get<double>()),
			static_cast<float>(values[1].get<double>()),
			static_cast<float>(values[2].get<double>())
		);
	}

	sibr::ManifestRuleType parseRuleType(const std::string& typeString) {
		if (typeString == "camera_bounds") {
			return sibr::ManifestRuleType::CameraBounds;
		}
		if (typeString == "distance") {
			return sibr::ManifestRuleType::Distance;
		}
		return sibr::ManifestRuleType::Phase;
	}

	bool loadJson(const boost::filesystem::path& path, picojson::value& value, std::string& error) {
		std::ifstream manifestFile(path.string());
		if (!manifestFile.good()) {
			error = "Unable to open manifest file: " + path.string();
			return false;
		}

		error = picojson::parse(value, manifestFile);
		if (!error.empty()) {
			return false;
		}
		if (!value.is<picojson::object>()) {
			error = "Manifest root must be a JSON object.";
			return false;
		}
		return true;
	}
}

namespace sibr {
	bool ManifestStore::load(const boost::filesystem::path& manifestPath)
	{
		clear();

		picojson::value rootValue;
		std::string error;
		if (!loadJson(manifestPath, rootValue, error)) {
			SIBR_WRG << error << std::endl;
			return false;
		}

		const auto& root = rootValue.get<picojson::object>();

		const auto globalIt = root.find("global");
		if (globalIt != root.end() && globalIt->second.is<picojson::object>()) {
			const auto& global = globalIt->second.get<picojson::object>();
			settings_.target_vram_bytes = parseSize(global, "target_vram_mb", 0) * 1024ull * 1024ull;
			settings_.target_ram_bytes = parseSize(global, "target_ram_mb", 0) * 1024ull * 1024ull;
			settings_.max_upload_bytes_per_frame = parseSize(global, "max_upload_mb_per_frame", 0) * 1024ull * 1024ull;
			settings_.max_gpu_evictions_per_frame = parseSize(global, "max_gpu_evictions_per_frame", settings_.max_gpu_evictions_per_frame);
			settings_.max_cpu_evictions_per_frame = parseSize(global, "max_cpu_evictions_per_frame", settings_.max_cpu_evictions_per_frame);
			settings_.max_concurrent_disk_loads = std::max(1, parseInt(global, "max_concurrent_disk_loads", settings_.max_concurrent_disk_loads));
			settings_.default_unload_hysteresis_sec = parseDouble(global, "default_unload_hysteresis_sec", settings_.default_unload_hysteresis_sec);
			settings_.warm_rule_assets_cpu = parseBool(global, "warm_rule_assets_cpu", settings_.warm_rule_assets_cpu);
		}

		const auto assetsIt = root.find("assets");
		if (assetsIt == root.end() || !assetsIt->second.is<picojson::object>()) {
			SIBR_WRG << "Manifest must contain an object field named 'assets'." << std::endl;
			return false;
		}

		const auto manifestDirectory = boost::filesystem::absolute(manifestPath).parent_path();
		for (const auto& assetPair : assetsIt->second.get<picojson::object>()) {
			if (!assetPair.second.is<picojson::object>()) {
				continue;
			}

			const auto& assetObject = assetPair.second.get<picojson::object>();

			AssetDescriptor descriptor;
			descriptor.id = assetPair.first;
			descriptor.model_dir = boost::filesystem::path(parseString(assetObject, "model_dir"));
			if (descriptor.model_dir.empty()) {
				SIBR_WRG << "Manifest asset '" << descriptor.id << "' is missing 'model_dir'." << std::endl;
				continue;
			}
			if (descriptor.model_dir.is_relative()) {
				descriptor.model_dir = manifestDirectory / descriptor.model_dir;
			}
			if (!boost::filesystem::exists(descriptor.model_dir)) {
				SIBR_WRG << "Manifest asset '" << descriptor.id << "' points to a missing model directory: "
					<< descriptor.model_dir.string() << std::endl;
				continue;
			}
			try {
				descriptor.model_dir = boost::filesystem::canonical(descriptor.model_dir);
			}
			catch (const boost::filesystem::filesystem_error& error) {
				SIBR_WRG << "Manifest asset '" << descriptor.id << "' failed to resolve model directory '"
					<< descriptor.model_dir.string() << "': " << error.what() << std::endl;
				continue;
			}
			descriptor.tags = parseStringArray(assetObject, "tags");
			descriptor.bounds_min = parseVector3(assetObject, "bounds_min", descriptor.bounds_min);
			descriptor.bounds_max = parseVector3(assetObject, "bounds_max", descriptor.bounds_max);
			descriptor.estimated_cpu_bytes = parseSize(assetObject, "estimated_cpu_bytes", 0);
			descriptor.estimated_gpu_bytes = parseSize(assetObject, "estimated_gpu_bytes", 0);
			descriptor.priority = parseInt(assetObject, "priority", 0);
			descriptor.pin_cpu = parseBool(assetObject, "pin_cpu", false);
			descriptor.pin_gpu = parseBool(assetObject, "pin_gpu", false);
			descriptor.prefetch_distance = static_cast<float>(parseDouble(assetObject, "prefetch_distance", 0.0));
			descriptor.unload_hysteresis_sec = static_cast<float>(parseDouble(
				assetObject,
				"unload_hysteresis_sec",
				settings_.default_unload_hysteresis_sec));

			assets_.emplace(descriptor.id, std::move(descriptor));
		}

		const auto rulesIt = root.find("rules");
		if (rulesIt != root.end() && rulesIt->second.is<picojson::array>()) {
			for (const auto& ruleValue : rulesIt->second.get<picojson::array>()) {
				if (!ruleValue.is<picojson::object>()) {
					continue;
				}

				const auto& ruleObject = ruleValue.get<picojson::object>();
				ManifestRule rule;
				rule.name = parseString(ruleObject, "name");
				rule.type = parseRuleType(parseString(ruleObject, "type", "phase"));
				rule.phase = parseString(ruleObject, "phase");
				rule.source = parseString(ruleObject, "source", "camera");
				rule.region_min = parseVector3(ruleObject, "region_min", rule.region_min);
				rule.region_max = parseVector3(ruleObject, "region_max", rule.region_max);
				rule.distance = static_cast<float>(parseDouble(ruleObject, "distance", 0.0));
				rule.required = parseStringArray(ruleObject, "required");
				rule.warm = parseStringArray(ruleObject, "warm");
				rule.required_by_tag = parseStringArray(ruleObject, "required_by_tag");
				rule.warm_by_tag = parseStringArray(ruleObject, "warm_by_tag");
				rules_.emplace_back(std::move(rule));
			}
		}

		for (const auto& rule : rules_) {
			referencedAssets_.insert(rule.required.begin(), rule.required.end());
			referencedAssets_.insert(rule.warm.begin(), rule.warm.end());

			for (const auto& tag : rule.required_by_tag) {
				for (const auto& assetPair : assets_) {
					if (containsTag(assetPair.second.tags, tag)) {
						referencedAssets_.insert(assetPair.first);
					}
				}
			}

			for (const auto& tag : rule.warm_by_tag) {
				for (const auto& assetPair : assets_) {
					if (containsTag(assetPair.second.tags, tag)) {
						referencedAssets_.insert(assetPair.first);
					}
				}
			}
		}

		manifestPath_ = boost::filesystem::absolute(manifestPath);
		SIBR_LOG << "Loaded manifest '" << manifestPath_.string() << "' with " << assets_.size()
			<< " asset(s) and " << rules_.size() << " rule(s)." << std::endl;
		return true;
	}

	void ManifestStore::clear()
	{
		manifestPath_.clear();
		settings_ = ManifestGlobalSettings();
		assets_.clear();
		rules_.clear();
		referencedAssets_.clear();
	}

	bool ManifestStore::empty() const
	{
		return assets_.empty();
	}

	const boost::filesystem::path& ManifestStore::path() const
	{
		return manifestPath_;
	}

	const ManifestGlobalSettings& ManifestStore::settings() const
	{
		return settings_;
	}

	const std::unordered_map<AssetId, AssetDescriptor>& ManifestStore::assets() const
	{
		return assets_;
	}

	const std::vector<ManifestRule>& ManifestStore::rules() const
	{
		return rules_;
	}

	const std::unordered_set<AssetId>& ManifestStore::referencedAssets() const
	{
		return referencedAssets_;
	}

	std::vector<PhaseId> ManifestStore::phases() const
	{
		std::set<PhaseId> uniquePhases;
		for (const auto& rule : rules_) {
			if (!rule.phase.empty()) {
				uniquePhases.insert(rule.phase);
			}
		}
		return std::vector<PhaseId>(uniquePhases.begin(), uniquePhases.end());
	}
}
