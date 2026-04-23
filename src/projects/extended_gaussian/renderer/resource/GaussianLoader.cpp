#include "GaussianLoader.hpp"

#include <fstream>
#include <regex>

namespace fs = boost::filesystem;

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

		return field;
	}	
}

