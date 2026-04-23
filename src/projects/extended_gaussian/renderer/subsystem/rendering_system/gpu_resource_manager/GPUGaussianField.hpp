#pragma once

#include <core/system/Config.hpp>
#include "Config.hpp"

#include <string>

namespace sibr {
	class GaussianField;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT GPUGaussianField {
	public:
		SIBR_CLASS_PTR(GPUGaussianField);

		GPUGaussianField(const std::string& p_assetId, const GaussianField* p_origin);

		GPUGaussianField(const GPUGaussianField&) = delete;
		GPUGaussianField& operator=(const GPUGaussianField&) = delete;

		~GPUGaussianField();

		std::string asset_id;
		int count;
		int sh_degree = 0;
		size_t bytes = 0;
		float* pos_cuda = nullptr;
		float* rot_cuda = nullptr;
		float* scale_cuda = nullptr;
		float* opacity_cuda = nullptr;
		float* shs_cuda = nullptr;
	};
}
