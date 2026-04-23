#include "RenderingSystem.hpp"

#include "RenderUtils.hpp"
#include <projects/extended_gaussian/renderer/ExtendedGaussianViewer.hpp>

namespace sibr {
	RenderingSystem::RenderingSystem()
	{
		type = RENDERING_SYSTEM;

		int num_devices;
		CUDA_SAFE_CALL_ALWAYS(cudaGetDeviceCount(&num_devices));
		device = 0;
		if (device >= num_devices)
		{
			if (num_devices == 0)
				SIBR_ERR << "No CUDA devices detected!";
			else
				SIBR_ERR << "Provided device index exceeds number of available CUDA devices!";
		}
		CUDA_SAFE_CALL_ALWAYS(cudaSetDevice(device));
		cudaDeviceProp prop;
		CUDA_SAFE_CALL_ALWAYS(cudaGetDeviceProperties(&prop, device));
		// Portable Windows bundles embed CUDA code for SM 7.5 (Turing) and newer.
		// Older devices pass initial CUDA setup but fail later at first kernel launch
		// with "no kernel image is available"; reject them here with a clear message.
		if (prop.major < 7 || (prop.major == 7 && prop.minor < 5))
		{
			SIBR_ERR << "Unsupported CUDA device: compute capability "
				<< prop.major << "." << prop.minor << " detected (" << prop.name << "). "
				<< "This portable bundle requires SM 7.5 (Turing) or newer. "
				<< "Rebuild from source with -DEXTENDED_GAUSSIAN_CUDA_ARCHITECTURES including your device architecture to use older GPUs.";
		}
	}

	void RenderingSystem::onSystemAdded(ExtendedGaussianViewer& owner)
	{
		resources = owner.getResourceManager();
		swapManager = std::make_unique<SwapManager>(*resources, GPUResourceManager::getInstance());

		// Create View
		Vector2i winSize = owner.getWindowSize();
		auto view = std::make_shared<GaussianView>(this, winSize.x(), winSize.y(), true);

		// View flag
		ImGuiWindowFlags flags = 
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | 
			ImGuiWindowFlags_AlwaysAutoResize |	ImGuiWindowFlags_ResizeFromAnySide;

		// Add View to Viewer
		owner.addIBRSubView("Gaussian View", view, { winSize.x(), winSize.y() }, flags);
		addView(view);

		// Create Camera for view
		auto startCam = std::make_shared<sibr::InputCamera>();
		startCam->setLookAt(Vector3f(5, 5, 5), Vector3f(0, 0, 0), Vector3f(0, 1, 0));
		startCam->fovy(0.785f);
		startCam->aspect((float)winSize.x() / winSize.y());
		startCam->znear(0.1f);
		startCam->zfar(1000.0f);

		auto handler = std::make_shared<sibr::InteractiveCameraHandler>();
		handler->setup({ startCam }, Viewport{0, 0, (float)winSize.x(), (float)winSize.y()}, nullptr);
		handler->fromCamera(*startCam);

		// Add Camera to Viewer
		owner.addCameraForView("Gaussian View", handler);

		scene = std::make_unique<RenderGaussianScene>();
	}

	void RenderingSystem::onSystemRemoved(ExtendedGaussianViewer& owner)
	{
	}

	void RenderingSystem::onInstanceCreated(GaussianInstance& instance)
	{
		scene->createInstance(&instance);
		if (RenderGaussianInstance* renderInstance = scene->getInstance(&instance)) {
			syncRenderInstanceAsset(*renderInstance, instance);
		}
	}

	void RenderingSystem::onInstanceUpdated(GaussianInstance& instance)
	{
		if (RenderGaussianInstance* render_instance = scene->getInstance(&instance)) {
			syncRenderInstanceAsset(*render_instance, instance);
		}
	}

	void RenderingSystem::onInstanceRemoved(GaussianInstance& instance)
	{
		scene->removeInstance(&instance);
	}

	void RenderingSystem::onRender() {}

	bool RenderingSystem::addView(GaussianView::Ptr view)
	{
		if (!view) return false;

		if (views.find(view->name()) != views.end()) {
			SIBR_WRG << "GaussianView with name '" << view->name() << "' already exists." << std::endl;
			return false;
		}

		views.emplace(view->name(), view);
		return true;
	}

	const GaussianView* RenderingSystem::getView(const std::string& name) const
	{
		auto itr = views.find(name);
		if (itr == views.end()) {
			return nullptr;
		}
		return itr->second.get();
	}

	GaussianView* RenderingSystem::getView(const std::string& name)
	{
		return const_cast<GaussianView*>(static_cast<const RenderingSystem*>(this)->getView(name));
	}

	bool RenderingSystem::removeView(const std::string& name)
	{
		auto itr = views.find(name);
		if (itr == views.end()) {
			SIBR_WRG << "Cannot remove: GaussianView '" << name << "' not found." << std::endl;
			return false;
		}

		views.erase(itr);
		return true;
	}

	const RenderGaussianScene* RenderingSystem::getScene() const {
		return scene.get();
	}

	void RenderingSystem::syncRenderInstanceAsset(RenderGaussianInstance& renderInstance, const GaussianInstance& instance)
	{
		const std::string& assetId = instance.getAssetId();
		renderInstance.setAssetId(assetId, nullptr);
	}

	void RenderingSystem::setManifest(const ManifestStore* manifest)
	{
		if (swapManager) {
			swapManager->setManifest(manifest);
		}
	}

	void RenderingSystem::ensureManualGpuResidency()
	{
		if (!resources || !scene) {
			return;
		}

		GPUResourceManager& gpuManager = GPUResourceManager::getInstance();
		for (const auto& instancePair : scene->getInstances()) {
			const auto* renderInstance = instancePair.second.get();
			if (!renderInstance) {
				continue;
			}

			const std::string& assetId = renderInstance->getAssetId();
			if (assetId.empty() || gpuManager.has(assetId)) {
				continue;
			}

			const auto cpuField = resources->getCpuFieldShared(assetId);
			if (cpuField) {
				gpuManager.addField(assetId, cpuField.get());
			}
		}
	}

	void RenderingSystem::tickStreaming(const ViewerContext& context)
	{
		if (swapManager && swapManager->hasManifest()) {
			if (const auto* gaussianView = getView("Gaussian View")) {
				swapManager->setSkippedInstancesLastFrame(gaussianView->lastSkippedInstances());
			}
			swapManager->tick(context);
			return;
		}
		ensureManualGpuResidency();
	}

	bool RenderingSystem::hasManifest() const
	{
		return swapManager && swapManager->hasManifest();
	}

	const SwapManager::Stats* RenderingSystem::getSwapStats() const
	{
		if (!swapManager) {
			return nullptr;
		}
		return &swapManager->stats();
	}

	RenderingSystem::~RenderingSystem()
	{
	}
}
