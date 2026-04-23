#pragma once

#include <vector>
# include <core/system/Config.hpp>
#include "Config.hpp"

namespace sibr {
	class SIBR_EXTENDED_GAUSSIAN_EXPORT GaussianSet {
	public:
		SIBR_CLASS_PTR(GaussianSet);

		GaussianSet() = default;
		~GaussianSet() = default;

		GaussianSet(const GaussianSet&) = delete;
		GaussianSet& operator=(const GaussianSet&) = delete;

		uint32_t start_index;
		uint32_t gaussian_count;
		Vector3f min_edges;
		Vector3f max_edges;
		std::vector<GaussianSet::UPtr> children;
	};
}