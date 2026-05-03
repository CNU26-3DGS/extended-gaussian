#pragma once

# include <core/system/Config.hpp>
#include "Config.hpp"
#include "GaussianSet.hpp"

namespace sibr {
	class SIBR_EXTENDED_GAUSSIAN_EXPORT GaussianField {
	public:
		SIBR_CLASS_PTR(GaussianField);

		GaussianField() = default;
		~GaussianField() = default;

		GaussianField(const GaussianField&) = delete;
		GaussianField& operator=(const GaussianField&) = delete;
	
		std::string path;
		std::string name;
		uint32_t count = 0;
		std::vector<Vector3f> pos;
		std::vector<Vector3f> scale;
		std::vector<Vector4f> rot;
		std::vector<float> opacities;
		std::vector<float> SHs;
		int sh_degree = 0;
		Vector3f min_edges = Vector3f::Zero();
		Vector3f max_edges = Vector3f::Zero();
		bool has_centroid = false;
		Vector3f centroid = Vector3f::Zero();
		bool has_focus_bounds = false;
		Vector3f focus_center = Vector3f::Zero();
		Vector3f focus_bounds_min = Vector3f::Zero();
		Vector3f focus_bounds_max = Vector3f::Zero();
		GaussianSet::UPtr root;
	};
}
