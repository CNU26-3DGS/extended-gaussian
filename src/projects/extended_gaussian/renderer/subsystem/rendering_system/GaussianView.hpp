#pragma once

#include "Config.hpp"
#include <core/renderer/RenderMaskHolder.hpp>
#include <core/scene/BasicIBRScene.hpp>
#include <core/system/SimpleTimer.hpp>
#include <core/system/Config.hpp>
#include <core/graphics/Mesh.hpp>
#include <core/view/ViewBase.hpp>
#include <core/renderer/CopyRenderer.hpp>
#include <core/renderer/PointBasedRenderer.hpp>
#include <memory>
#include <core/graphics/Texture.hpp>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <functional>
#include <vector>

namespace CudaRasterizer
{
	class Rasterizer;
}

namespace sibr {
	class RenderingSystem;
	class BufferCopyRenderer;
	class GPUGaussianField;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT GaussianView : public sibr::ViewBase
	{
	public:
		SIBR_CLASS_PTR(GaussianView);

		GaussianView(const RenderingSystem* p_owner, uint render_w, uint render_h, bool useInterop);

		virtual ~GaussianView() override;

		void onRenderIBR(sibr::IRenderTarget& dst, const sibr::Camera& eye) override;

		void onGUI() override;
		size_t lastSkippedInstances() const;
		void releaseTransientBuffers();
		size_t worldBufferBytes() const;
		size_t scratchBufferBytes() const;
		size_t outputInteropBytes() const;

	private:
		bool resizeWorldBuffersIfNeeded(size_t count);
		void releaseScratchBuffers();
		void releaseWorldBuffers();
		void AppendGaussianToWorld(
			const GPUGaussianField* source,
			size_t offset,
			const Vector3f& position,
			const Vector3f& euler,
			float scale
		);
		void TransformPosRotScaleToWorld(
			const GPUGaussianField* source,
			size_t offset,
			const Vector3f& position,
			const Vector3f& euler,
			float scale,
			float* w_pos_ptr, float* w_rot_ptr, float* w_scale_ptr
		);
		void appendSHsToWorld(const float* src_shs, int count, int offset, int sh_degree);
		void appendOpacitiesToWorld(const float* src_opacities, int count, int offset);

		const RenderingSystem* owner;

		bool antialiasing = false;
		bool cropping = false;
		bool fastCulling = true;

		GLuint imageBuffer;
		cudaGraphicsResource_t imageBufferCuda;

		size_t allocdGeom = 0, allocdBinning = 0, allocdImg = 0;
		void* geomPtr = nullptr, * binningPtr = nullptr, * imgPtr = nullptr;

		float background[3];

		size_t current_world_gausians_count = 0;
		size_t max_gaussians_count = 0;
		float* world_pos_cuda = nullptr;
		float* world_rot_cuda = nullptr;
		float* world_scale_cuda = nullptr;
		float* world_opacity_cuda = nullptr;
		float* world_shs_cuda = nullptr;
		int* world_rect_cuda = nullptr;

		float* view_cuda;
		float* proj_cuda;
		float* cam_pos_cuda;
		float* background_cuda;

		bool _interop_failed = false;
		std::vector<char> fallback_bytes;
		float* fallbackBufferCuda = nullptr;
		std::function<char* (size_t N)> geomBufferFunc, binningBufferFunc, imgBufferFunc;
		size_t lastSkippedInstances_ = 0;

		BufferCopyRenderer* copyRenderer;
	};

} /*namespace sibr*/
