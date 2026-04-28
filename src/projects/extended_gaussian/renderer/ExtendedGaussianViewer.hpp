#pragma once

# include <core/system/Config.hpp>
# include <core/view/MultiViewManager.hpp>
# include "Config.hpp"
#include <projects/extended_gaussian/renderer/scene/GaussianScene.hpp>
#include <projects/extended_gaussian/renderer/subsystem/Subsystem.hpp>
#include <projects/extended_gaussian/renderer/resource/ManifestStore.hpp>
#include <projects/extended_gaussian/renderer/resource/ResourceManager.hpp>
#include <projects/extended_gaussian/renderer/subsystem/rendering_system/gpu_resource_manager/GPUResourceManager.hpp>

namespace sibr {
	class RenderingSystem;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT ExtendedGaussianViewer : public sibr::MultiViewBase
	{
	public:
		SIBR_CLASS_PTR(ExtendedGaussianViewer);

		/*
		 * \brief Creates a MultiViewManager in a given OS window.
		 * \param window The OS window to use.
		 * \param resize Should the window be resized by the manager to maximize usable space.
		 */
		ExtendedGaussianViewer(sibr::Window& window, bool resize = true);

		/**
		 * \brief Update subviews and the MultiViewManager.
		 * \param input The Input state to use.
		 */
		void	onUpdate(sibr::Input& input) override;

		/**
		 * \brief Render the content of the MultiViewManager and its interface
		 * \param win The OS window into which the rendering should be performed.
		 */
		void	onRender(sibr::Window& win) override;

		/**
		 * \brief Render menus and additional gui
		 * \param win The OS window into which the rendering should be performed.
		 */
		void	onGui(sibr::Window& win) override;

		void onSwapBuffer(sibr::Window& win);

		Vector2i getWindowSize() const;

		const GaussianScene* getScene() const;
		GaussianScene* getScene();
		ResourceManager* getResourceManager();
		const ResourceManager* getResourceManager() const;
		RenderingSystem* getRenderingSystem();
		const RenderingSystem* getRenderingSystem() const;

	private:
		bool loadManifestFile(const std::string& path);
		size_t createManifestInstances(bool onlyMissing = true);
		bool canFocusBlockCenter() const;
		bool focusCameraOnBlockCenter();
		bool focusCameraOnManifestBounds();
		bool focusCameraOnBounds(const Vector3f& minBounds, const Vector3f& maxBounds);
		bool getManifestBounds(Vector3f& minBounds, Vector3f& maxBounds) const;
		bool getInstanceWorldBounds(const GaussianInstance& instance, Vector3f& minBounds, Vector3f& maxBounds) const;
		static const char* cpuStateLabel(CpuState state);
		static const char* gpuStateLabel(GpuState state);
		static std::string formatMegabytes(size_t bytes);

		void onShowScenePanel(sibr::Window& win);
		void onShowResourceBrowser(sibr::Window& win);

		/** Show/hide the GUI. */
		void toggleGUI();

		sibr::Window& _window; ///< The OS window.
		sibr::FPSCounter _fpsCounter; ///< A FPS counter.
		bool _showGUI = true; ///< Should the GUI be displayed.
		bool _showScenePanel = false;
		bool _showResourceBrowser = false;

		GaussianInstance* _selectedInstance = nullptr;
		std::string _selectedField;

		GaussianScene::UPtr _scene;
		Subsystem::UPtr _subsystem[SubsystemType::SUBSYSTEM_LAST];
		ResourceManager::UPtr _resourceManager;
		ManifestStore _manifestStore;
		std::string _loadedManifestPath;
		std::string _currentPhase;
		double _appTimeSec = 0.0;
		uint64_t _frameIndex = 0;
	};
}

