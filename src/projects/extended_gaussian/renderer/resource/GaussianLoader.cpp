#include "GaussianLoader.hpp"

#include "picojson/picojson.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <regex>
#include <vector>

namespace fs = boost::filesystem;

namespace {
	bool isFinite(const sibr::Vector3d& value)
	{
		return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
	}

	bool parseVector3(const picojson::value& value, sibr::Vector3d& out)
	{
		if (!value.is<picojson::array>()) {
			return false;
		}

		const auto& array = value.get<picojson::array>();
		if (array.size() != 3 || !array[0].is<double>() || !array[1].is<double>() || !array[2].is<double>()) {
			return false;
		}

		out = sibr::Vector3d(array[0].get<double>(), array[1].get<double>(), array[2].get<double>());
		return isFinite(out);
	}
}

std::string findLargestNumberedSubdirectory(const std::string& directoryPath) {
	fs::path dirPath(directoryPath);
	if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
		std::cerr << "Invalid directory: " << directoryPath << std::endl;
		return "";
	}

	std::regex regexPattern(R"_(iteration_(\d+))_");
	std::string largestSubdirectory;
	int largestNumber = -1;

	for (const auto& entry : fs::directory_iterator(dirPath)) {
		if (fs::is_directory(entry)) {
			std::string subdirectory = entry.path().filename().string();
			std::smatch match;

			if (std::regex_match(subdirectory, match, regexPattern)) {
				int number = std::stoi(match[1]);

				if (number > largestNumber) {
					largestNumber = number;
					largestSubdirectory = subdirectory;
				}
			}
		}
	}

	return largestSubdirectory;
}

std::string findPointCloudRoot(const std::string& modelPath)
{
	const fs::path root(modelPath);
	const fs::path standardRoot = root / "point_cloud";
	if (fs::exists(standardRoot) && fs::is_directory(standardRoot)) {
		return standardRoot.string();
	}

	const fs::path blockRoot = root / "point_cloud_blocks";
	if (fs::exists(blockRoot) && fs::is_directory(blockRoot)) {
		const fs::path preferredScale = blockRoot / "scale_1.0";
		if (fs::exists(preferredScale) && fs::is_directory(preferredScale)) {
			return preferredScale.string();
		}

		for (const auto& entry : fs::directory_iterator(blockRoot)) {
			if (fs::is_directory(entry)) {
				return entry.path().string();
			}
		}
	}

	return "";
}

std::pair<int, int> findArg(const std::string& line, const std::string& name)
{
	int start = line.find(name, 0);
	start = line.find("=", start);
	start += 1;
	int end = line.find_first_of(",)", start);
	return std::make_pair(start, end);
}

namespace sibr {
	bool GaussianLoader::loadCameraFocus(const boost::filesystem::path& modelPath, GaussianField& output)
	{
		const fs::path cameraJsonPath = modelPath / "cameras.json";
		if (!fs::exists(cameraJsonPath)) {
			return false;
		}

		std::ifstream cameraFile(cameraJsonPath.string(), std::ios::in);
		if (!cameraFile.good()) {
			SIBR_WRG << "Unable to open cameras.json for camera focus: " << cameraJsonPath.string() << std::endl;
			return false;
		}

		picojson::value root;
		const std::string error = picojson::parse(root, cameraFile);
		if (!error.empty() || !root.is<picojson::array>()) {
			SIBR_WRG << "Unable to parse cameras.json for camera focus: " << cameraJsonPath.string() << std::endl;
			return false;
		}

		Vector3d positionSum = Vector3d::Zero();
		size_t cameraCount = 0;
		for (const auto& frame : root.get<picojson::array>()) {
			if (!frame.is<picojson::object>()) {
				continue;
			}

			const auto& object = frame.get<picojson::object>();
			const auto positionIt = object.find("position");
			if (positionIt == object.end()) {
				continue;
			}

			Vector3d position = Vector3d::Zero();
			if (!parseVector3(positionIt->second, position)) {
				continue;
			}
			positionSum += position;
			++cameraCount;
		}

		if (cameraCount == 0) {
			SIBR_WRG << "Unable to compute camera position center from " << cameraJsonPath.string() << std::endl;
			return false;
		}

		output.has_camera_focus_center = true;
		output.camera_focus_center = (positionSum / static_cast<double>(cameraCount)).cast<float>();
		SIBR_LOG << "Computed camera position center from " << cameraCount << " camera(s): "
			<< output.camera_focus_center.transpose() << std::endl;
		return true;
	}

	GaussianField::UPtr GaussianLoader::load(const std::string& modelPath) {
		auto field = std::make_unique<GaussianField>();
		field->path = modelPath;

		fs::path p(modelPath);

		std::string folderName = p.filename().string();
		if (folderName.empty()) {
			folderName = p.parent_path().filename().string();
		}

		field->name = folderName;

		// 1. ��� ó��
		std::string pathWithSlash = modelPath;
		if (pathWithSlash.back() != '/' && pathWithSlash.back() != '\\') {
			pathWithSlash += "/";
		}

		// 2. cfg_args �Ľ�
		std::ifstream cfgFile(pathWithSlash + "cfg_args");
		if (!cfgFile.good()) {
			SIBR_ERR << "Could not find config file 'cfg_args' at " << modelPath;
			return nullptr;
		}

		std::string cfgLine;
		std::getline(cfgFile, cfgLine);
		auto shRng = findArg(cfgLine, "sh_degree");
		int runtime_sh_degree = std::stoi(cfgLine.substr(shRng.first, shRng.second - shRng.first));
		field->sh_degree = runtime_sh_degree; // field�� ����

		// 3. �ֽ� iteration ���� ã��
		std::string plyRoot = findPointCloudRoot(modelPath);
		if (plyRoot.empty()) {
			SIBR_ERR << "Could not find point cloud directory at " << modelPath
				<< " (expected 'point_cloud' or 'point_cloud_blocks/scale_*').";
			return nullptr;
		}
		std::string latestFolder = findLargestNumberedSubdirectory(plyRoot);
		if (latestFolder.empty()) {
			SIBR_ERR << "Could not find iteration folder in " << plyRoot;
			return nullptr;
		}

		// 4. ���� PLY ��� �ϼ�
		std::string finalPlyPath = plyRoot + "/" + latestFolder + "/point_cloud.ply";

		// 5. [�ٽ� ����] ��Ÿ�� ������ ������ Ÿ�� ����� ����
		bool success = false;
		switch (runtime_sh_degree) {
		case 0: success = loadPly<0>(finalPlyPath.c_str(), *field); break;
		case 1: success = loadPly<1>(finalPlyPath.c_str(), *field); break;
		case 2: success = loadPly<2>(finalPlyPath.c_str(), *field); break;
		case 3: success = loadPly<3>(finalPlyPath.c_str(), *field); break;
		default:
			SIBR_ERR << "Unsupported SH degree: " << runtime_sh_degree;
			return nullptr;
		}

		if (!success) return nullptr;

		loadCameraFocus(p, *field);

		return field;
	}	
}
