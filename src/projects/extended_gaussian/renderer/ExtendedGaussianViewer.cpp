
#include "ExtendedGaussianViewer.hpp"
#include <projects/extended_gaussian/renderer/resource/GaussianLoader.hpp>
#include <projects/extended_gaussian/renderer/subsystem/rendering_system/RenderingSystem.hpp>
#include <projects/extended_gaussian/renderer/subsystem/capture_system/CaptureSystem.hpp>

#include <core/system/CommandLineArgs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cuda_runtime.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace sibr {
	namespace {
		int clampShDegree(int degree)
		{
			return std::max(0, std::min(3, degree));
		}

		float clampFloat(float value, float minimum, float maximum)
		{
			return std::max(minimum, std::min(maximum, value));
		}

		struct UserUILayout {
			Vector2f winSize = Vector2f(1.0f, 1.0f);
			float dpiScale = 1.0f;
			float resolutionScale = 1.0f;
			float scale = 1.0f;
			float margin = 24.0f;
		};

		UserUILayout getUserUILayout(const Window& win)
		{
			UserUILayout layout;
			layout.winSize = win.size().cast<float>();
			layout.winSize.x() = std::max(1.0f, layout.winSize.x());
			layout.winSize.y() = std::max(1.0f, layout.winSize.y());
			layout.dpiScale = std::max(1.0f, ImGui::GetIO().FontGlobalScale);

			const float baseWidth = 1280.0f * layout.dpiScale;
			const float baseHeight = 800.0f * layout.dpiScale;
			const float widthScale = layout.winSize.x() / baseWidth;
			const float heightScale = layout.winSize.y() / baseHeight;
			layout.resolutionScale = clampFloat(std::min(widthScale, heightScale), 0.72f, 1.45f);
			layout.scale = layout.dpiScale * layout.resolutionScale;
			layout.margin = 24.0f * layout.scale;
			return layout;
		}

		float fitUserDimension(float preferred, float minimum, float available)
		{
			available = std::max(1.0f, available);
			return std::min(std::max(preferred, minimum), available);
		}
	}

	// SIBR 핸들러 상속
	class CustomCameraHandler : public sibr::InteractiveCameraHandler {
	protected:
		sibr::InputCamera _myCam;
		sibr::Vector3f _worldUp;

	public:
		void fromCamera(const sibr::InputCamera& cam, bool anim = false, bool force = false) {
			_myCam = cam;
			_worldUp = cam.up();
			sibr::InteractiveCameraHandler::fromCamera(cam, anim, force);
		}

		const sibr::InputCamera& getCamera(void) const override {
			return _myCam;
		}

		float customCameraSpeed = 10.0f;
		float customRotSpeed = 10.0f;
		sibr::Vector2f uiMoveVector = sibr::Vector2f(0.0f, 0.0f);

		void setUiMoveVector(const sibr::Vector2f& moveVector) {
			uiMoveVector = moveVector;
			const float length = uiMoveVector.norm();
			if (length > 1.0f) {
				uiMoveVector /= length;
			}
		}

		void update(const sibr::Input& input, const float deltaTime, const sibr::Viewport& viewport) override {

			if (viewport.finalSize().y() > 0.0f) { // 사이즈 조절
				_myCam.aspect(viewport.finalSize().x() / (float)viewport.finalSize().y());
			}

			sibr::Vector2f mouseDelta = input.mouseDeltaPosition().cast<float>();
			sibr::Vector3f pos = _myCam.position();

			// 이동 속도;
			float currentMoveSpeed = customCameraSpeed * 0.05f * deltaTime; // 필요시 flaot 조절

			// 회전 속도
			float currentRotSens = customRotSpeed * 0.0001f; // 필요시 float 조절

			// 키보드 이동
			if (input.key().isActivated(sibr::Key::W)) pos += _myCam.dir() * currentMoveSpeed;
			if (input.key().isActivated(sibr::Key::S)) pos -= _myCam.dir() * currentMoveSpeed;
			if (input.key().isActivated(sibr::Key::A)) pos -= _myCam.right() * currentMoveSpeed;
			if (input.key().isActivated(sibr::Key::D)) pos += _myCam.right() * currentMoveSpeed;
			if (input.key().isActivated(sibr::Key::E)) pos += _worldUp * currentMoveSpeed;
			if (input.key().isActivated(sibr::Key::Q)) pos -= _worldUp * currentMoveSpeed;
			pos += _myCam.right() * (uiMoveVector.x() * currentMoveSpeed);
			pos += _myCam.dir() * (uiMoveVector.y() * currentMoveSpeed);

			// 마우스 이동
			const bool usingUiMove = uiMoveVector.squaredNorm() > 0.0001f;
			if (!usingUiMove && input.mouseButton().isActivated(sibr::Mouse::Left)) {
				float slideSens = (currentMoveSpeed * 0.01f); // 필요시 float 조절
				sibr::Vector3f right = _myCam.right();
				sibr::Vector3f up = _myCam.up();

				pos += (-mouseDelta.x() * slideSens * right) + (mouseDelta.y() * slideSens * up);
				_myCam.setLookAt(pos, pos + _myCam.dir(), _myCam.up());
			}
			// 마우스 회전
			else if (!usingUiMove && input.mouseButton().isActivated(sibr::Mouse::Right)) {
				sibr::Vector3f forward = _myCam.dir();
				sibr::Vector3f right = _myCam.right();
				sibr::Vector3f up = _myCam.up();

				sibr::Matrix3f yawRot = Eigen::AngleAxisf(mouseDelta.x() * currentRotSens, _worldUp).toRotationMatrix();
				sibr::Matrix3f pitchRot = Eigen::AngleAxisf(mouseDelta.y() * currentRotSens, right).toRotationMatrix();

				sibr::Vector3f newForward = yawRot * pitchRot * forward;
				sibr::Vector3f newUp = yawRot * pitchRot * up;

				// 짐벌락 방지
				if (std::abs(newForward.dot(_worldUp)) > 0.98f) {
					newForward = yawRot * forward;
					newUp = yawRot * up;
				}

				_myCam.setLookAt(pos, pos + newForward, newUp);
			}
			else {
				_myCam.setLookAt(pos, pos + _myCam.dir(), _myCam.up());
			}
		}
	};

	ExtendedGaussianViewer::ExtendedGaussianViewer(Window& window, bool resize, UIMode uiMode)
		: _window(window), _fpsCounter(false), _uiMode(uiMode)
	{
		_enableGUI = window.isGUIEnabled();

		if (resize) {
			window.size(
				Window::desktopSize().x() - 200,
				Window::desktopSize().y() - 200);
			window.position(100, 100);
		}

		/// \todo TODO: support launch arg for stereo mode.
		renderingMode(IRenderingMode::Ptr(new MonoRdrMode()));

		//Default view resolution.
		int w = int(window.size().x() * 0.5f);
		int h = int(window.size().y() * 0.5f);
		setDefaultViewResolution(Vector2i(w, h));

		if (_enableGUI)
		{
			ImGui::GetStyle().WindowBorderSize = 0.0;
		}
		if (_uiMode == UIMode::User) {
			_showSubViewsGui = false;
			_showScenePanel = false;
			_showResourceBrowser = false;
			_showCapturePanel = false;
			_showCameraSpeedPannel = false;
		}

		_maxShDegree = clampShDegree(getCommandLineArgs().get<int>("max-sh-degree", 1));
		CommandLineArgs::getGlobal().registerCommand(
			"max-sh-degree",
			"maximum spherical harmonics degree uploaded to GPU assets (0-3)",
			"1");

		_scene = std::make_unique<GaussianScene>();
		_resourceManager = std::make_unique<ResourceManager>();
		auto renderingSystem = std::make_unique<RenderingSystem>();
		renderingSystem->setMaxShDegree(_maxShDegree);
		_subsystem[RENDERING_SYSTEM] = std::move(renderingSystem);
		_subsystem[RENDERING_SYSTEM]->onSystemAdded(*this);

		const std::string manifestPath = getCommandLineArgs().get<std::string>("manifest", "");
		if (!manifestPath.empty()) {
			loadManifestFile(manifestPath);
		}
		if (_uiMode == UIMode::User) {
			_userStartupSelectionOpen = manifestPath.empty() || _manifestStore.empty();
		}

		_subsystem[CAPTURE_SYSTEM] = std::make_unique<CaptureSystem>();
		_subsystem[CAPTURE_SYSTEM]->onSystemAdded(*this);
	}

	void ExtendedGaussianViewer::onUpdate(Input& input)
	{
		auto viewIt = _ibrSubViews.find("Gaussian View");
		if (viewIt != _ibrSubViews.end()) {
			static bool isHandlerSwapped = false;

			if (!isHandlerSwapped) {
				auto myCustomHandler = std::make_shared<CustomCameraHandler>();
				myCustomHandler->fromCamera(viewIt->second.cam);

				viewIt->second.handler = myCustomHandler;
				isHandlerSwapped = true;
			}
		}

		MultiViewBase::onUpdate(input);
		_appTimeSec += deltaTime();

		if (input.key().isActivated(Key::LeftControl) && input.key().isActivated(Key::LeftAlt) && input.key().isReleased(Key::G)) {
			toggleGUI();
		}
	}

	void ExtendedGaussianViewer::onRender(Window& win)
	{
		win.viewport().bind();
		glClearColor(37.f / 255.f, 37.f / 255.f, 38.f / 255.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(1.f, 1.f, 1.f, 1.f);

		if (_uiMode == UIMode::Developer) {
			onGui(win);
		}

		RenderingSystem* renderingSystem = getRenderingSystem();
		if (renderingSystem) {
			ViewerContext context;
			const auto viewIt = _ibrSubViews.find("Gaussian View");
			if (viewIt != _ibrSubViews.end()) {
				context.camera_pos = viewIt->second.cam.position();
				context.camera_forward = viewIt->second.cam.dir();
				context.camera_up = viewIt->second.cam.up();
			}
			context.current_phase = _currentPhase;
			context.app_time_sec = _appTimeSec;
			context.dt_sec = deltaTime();
			context.frame_index = _frameIndex;
			renderingSystem->tickStreaming(context);
		}

		MultiViewBase::onRender(win);
		if (_uiMode == UIMode::User && _showGUI) {
			if (_userStartupSelectionOpen) {
				onShowUserStartupSelection(win);
				setUserCameraMoveVector(Vector2f(0.0f, 0.0f));
			}
			else {
				onShowUserMinimap(win);
				onShowUserInstanceList(win);
				onShowUserNavigationControl(win);
			}
		}
		else if (_uiMode == UIMode::User) {
			setUserCameraMoveVector(Vector2f(0.0f, 0.0f));
		}
		++_frameIndex;

		_fpsCounter.update(_enableGUI && _showGUI && _uiMode == UIMode::Developer);
	}

	void ExtendedGaussianViewer::onGui(Window& win)
	{
		MultiViewBase::onGui(win);
		if (_uiMode == UIMode::User) {
			return;
		}

		if (_showCameraSpeedPannel) {
			ImGui::Begin("Camera Speed", &_showCameraSpeedPannel);
			ImGui::Text("Movement Speed");
			ImGui::Separator();

			static float cameraSpeed = 20.0f;

			ImGui::SliderFloat("Movement Slider", &cameraSpeed, 1.0f, 100.0f, "%.1f");
			ImGui::Spacing();

			if (ImGui::Button("-##Move")) {
				cameraSpeed -= 0.1f;
			}

			ImGui::SameLine();

			if (ImGui::Button("+##Move")) {
				cameraSpeed += 0.1f;
			}

			ImGui::SameLine();
			ImGui::PushItemWidth(150.0f);
			ImGui::DragFloat("##MoveDrag", &cameraSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
			ImGui::PopItemWidth();

			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::Text("Rotation Speed");
			ImGui::Separator();

			static float cameraRotSpeed = 20.0f;
			ImGui::SliderFloat("Rotation (Slider)", &cameraRotSpeed, 1.0f, 100.0f, "%.1f");
			ImGui::Spacing();

			if (ImGui::Button("-##Rot")) { cameraRotSpeed -= 0.1f; }
			ImGui::SameLine();
			if (ImGui::Button("+##Rot")) { cameraRotSpeed += 0.1f; }

			ImGui::SameLine();
			ImGui::PushItemWidth(150.0f);
			ImGui::DragFloat("##RotDrag", &cameraRotSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
			ImGui::PopItemWidth();

			auto viewIt = _ibrSubViews.find("Gaussian View");
			if (viewIt != _ibrSubViews.end()) {

				auto handler = std::dynamic_pointer_cast<CustomCameraHandler>(viewIt->second.handler);
				if (handler) {
					handler->customCameraSpeed = cameraSpeed;
				}
			}

			ImGui::End();
		}

		// Menu
		if (_showGUI && ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("Menu"))
			{
				ImGui::MenuItem("Pause", "", &_onPause);
				if (ImGui::BeginMenu("Display")) {
					const bool currentScreenState = win.isFullscreen();
					if (ImGui::MenuItem("Fullscreen", "", currentScreenState)) {
						win.setFullscreen(!currentScreenState);
					}

					const bool currentSyncState = win.isVsynced();
					if (ImGui::MenuItem("V-sync", "", currentSyncState)) {
						win.setVsynced(!currentSyncState);
					}

					const bool isHiDPI = ImGui::GetIO().FontGlobalScale > 1.0f;
					if (ImGui::MenuItem("HiDPI", "", isHiDPI)) {
						if (isHiDPI) {
							ImGui::GetStyle().ScaleAllSizes(1.0f / win.scaling());
							ImGui::GetIO().FontGlobalScale = 1.0f;
						}
						else {
							ImGui::GetStyle().ScaleAllSizes(win.scaling());
							ImGui::GetIO().FontGlobalScale = win.scaling();
						}
					}

					if (ImGui::MenuItem("Hide GUI (!)", "Ctrl+Alt+G")) {
						toggleGUI();
					}
					ImGui::EndMenu();
				}


				if (ImGui::MenuItem("Mosaic layout")) {
					mosaicLayout(win.viewport());
				}

				if (ImGui::MenuItem("Row layout")) {
					Vector2f itemSize = win.size().cast<float>();
					itemSize[0] = std::round(float(itemSize[0]) / float(_subViews.size() + _ibrSubViews.size()));
					const float verticalShift = ImGui::GetTitleBarHeight();
					float vid = 0.0f;
					for (auto& view : _ibrSubViews) {
						// Compute position on grid.
						view.second.viewport = Viewport(vid * itemSize[0], verticalShift, (vid + 1.0f) * itemSize[0] - 1.0f, verticalShift + itemSize[1] - 1.0f);
						view.second.shouldUpdateLayout = true;
						++vid;
					}
					for (auto& view : _subViews) {
						// Compute position on grid.
						view.second.viewport = Viewport(vid * itemSize[0], verticalShift, (vid + 1.0f) * itemSize[0] - 1.0f, verticalShift + itemSize[1] - 1.0f);
						view.second.shouldUpdateLayout = true;
						++vid;
					}
				}


				if (ImGui::MenuItem("Quit", "Escape")) { win.close(); }
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Views"))
			{
				for (auto& subview : _subViews) {
					if (ImGui::MenuItem(subview.first.c_str(), "", subview.second.view->active())) {
						subview.second.view->active(!subview.second.view->active());
					}
				}
				for (auto& subview : _ibrSubViews) {
					if (ImGui::MenuItem(subview.first.c_str(), "", subview.second.view->active())) {
						subview.second.view->active(!subview.second.view->active());
					}
				}
				if (ImGui::MenuItem("Metrics", "", _fpsCounter.active())) {
					_fpsCounter.toggleVisibility();
				}
				if (ImGui::BeginMenu("Front when focus"))
				{
					for (auto& subview : _subViews) {
						const bool isLockedInBackground = subview.second.flags & ImGuiWindowFlags_NoBringToFrontOnFocus;
						if (ImGui::MenuItem(subview.first.c_str(), "", !isLockedInBackground)) {
							if (isLockedInBackground) {
								subview.second.flags &= ~ImGuiWindowFlags_NoBringToFrontOnFocus;
							}
							else {
								subview.second.flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
							}
						}
					}
					for (auto& subview : _ibrSubViews) {
						const bool isLockedInBackground = subview.second.flags & ImGuiWindowFlags_NoBringToFrontOnFocus;
						if (ImGui::MenuItem(subview.first.c_str(), "", !isLockedInBackground)) {
							if (isLockedInBackground) {
								subview.second.flags &= ~ImGuiWindowFlags_NoBringToFrontOnFocus;
							}
							else {
								subview.second.flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
							}
						}
					}
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Reset Settings to Default", "")) {
					_window.resetSettingsToDefault();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Capture"))
			{

				if (ImGui::MenuItem("Set export directory...")) {
					std::string selectedDirectory;
					if (showFilePicker(selectedDirectory, FilePickerMode::Directory)) {
						if (!selectedDirectory.empty()) {
							_exportPath = selectedDirectory;
						}
					}
				}

				for (auto& subview : _subViews) {
					if (ImGui::MenuItem(subview.first.c_str())) {
						captureView(subview.second, _exportPath);
					}
				}
				for (auto& subview : _ibrSubViews) {
					if (ImGui::MenuItem(subview.first.c_str())) {
						captureView(subview.second, _exportPath);
					}
				}

				if (ImGui::MenuItem("Export Video")) {
					std::string saveFile;
					if (showFilePicker(saveFile, FilePickerMode::Save)) {
						const std::string outputVideo = saveFile + ".mp4";
						if (!_videoFrames.empty()) {
							SIBR_LOG << "Exporting video to : " << outputVideo << " ..." << std::flush;
							FFVideoEncoder vdoEncoder(outputVideo, 30, Vector2i(_videoFrames[0].cols, _videoFrames[0].rows));
							for (int i = 0; i < _videoFrames.size(); i++) {
								vdoEncoder << _videoFrames[i];
							}
							_videoFrames.clear();
							std::cout << " Done." << std::endl;

						}
						else {
							SIBR_WRG << "No frames to export!! Check save frames in camera options for the view you want to render and play the path and re-export!" << std::endl;
						}
					}
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Panels"))
			{
				ImGui::MenuItem("Scene Outliner", nullptr, &_showScenePanel);
				ImGui::MenuItem("Resource Browser", nullptr, &_showResourceBrowser);
				ImGui::MenuItem("Classroom", nullptr, &_showCapturePanel);
				ImGui::MenuItem("Camera Speed", nullptr, &_showCameraSpeedPannel);
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}
		if (_showScenePanel)
		{
			onShowScenePanel(win);
		}
		if (_showResourceBrowser)
		{
			onShowResourceBrowser(win);
		}
		if (_showCapturePanel) {
			onShowCapturePanel(win);
		}
	}

	void ExtendedGaussianViewer::onSwapBuffer(sibr::Window& win)
	{
		win.swapBuffer();
	}

	Vector2i ExtendedGaussianViewer::getWindowSize() const
	{
		return _window.size();
	}

	const GaussianScene* ExtendedGaussianViewer::getScene() const {
		return _scene.get();
	}

	GaussianScene* ExtendedGaussianViewer::getScene() {
		return _scene.get();
	}

	ResourceManager* ExtendedGaussianViewer::getResourceManager() {
		return _resourceManager.get();
	}

	const ResourceManager* ExtendedGaussianViewer::getResourceManager() const {
		return _resourceManager.get();
	}

	RenderingSystem* ExtendedGaussianViewer::getRenderingSystem()
	{
		return static_cast<RenderingSystem*>(_subsystem[RENDERING_SYSTEM].get());
	}

	const RenderingSystem* ExtendedGaussianViewer::getRenderingSystem() const
	{
		return static_cast<const RenderingSystem*>(_subsystem[RENDERING_SYSTEM].get());
	}

	bool ExtendedGaussianViewer::loadManifestFile(const std::string& path)
	{
		if (!_manifestStore.load(path)) {
			_loadedManifestPath.clear();
			if (auto* renderingSystem = getRenderingSystem()) {
				renderingSystem->setManifest(nullptr);
			}
			_resourceBrowserStatus = "Manifest load failed. Check that the JSON exists and contains valid model_dir paths.";
			return false;
		}

		_loadedManifestPath = _manifestStore.path().string();
		_resourceManager->registerManifest(_manifestStore);
		auto phases = _manifestStore.phases();
		if (!phases.empty() && std::find(phases.begin(), phases.end(), _currentPhase) == phases.end()) {
			_currentPhase = phases.front();
		}

		if (auto* renderingSystem = getRenderingSystem()) {
			renderingSystem->setManifest(&_manifestStore);
		}

		if (_scene && _scene->getInstances().empty()) {
			const size_t createdCount = createManifestInstances(true);
			if (createdCount > 0) {
				SIBR_LOG << "Created " << createdCount << " manifest instance(s)." << std::endl;
			}
		}

		bool focused = false;
		const auto& initialFocusAsset = _manifestStore.settings().initial_focus_asset;
		if (!initialFocusAsset.empty()) {
			focused = focusCameraOnAssetCenter(initialFocusAsset);
			if (!focused) {
				SIBR_WRG << "Manifest initial_focus_asset '" << initialFocusAsset
					<< "' could not be focused. Falling back to manifest bounds." << std::endl;
			}
		}
		if (!focused) {
			focusCameraOnManifestBounds();
		}
		_resourceBrowserStatus = "Manifest loaded: " + std::to_string(_manifestStore.assets().size()) + " asset(s).";
		return true;
	}

	bool ExtendedGaussianViewer::importUserPlyFile(const std::string& path)
	{
		if (!_resourceManager || !_scene) {
			_userStartupSelectionStatus = "Viewer is not ready.";
			return false;
		}

		auto field = GaussianLoader::loadPlyFile(path);
		if (!field) {
			_userStartupSelectionStatus = "PLY load failed.";
			return false;
		}

		std::string baseName = field->name.empty() ? "ImportedPLY" : field->name;
		std::string assetId = baseName;
		int suffix = 1;
		while (_resourceManager->hasAsset(assetId)) {
			assetId = baseName + "_" + std::to_string(suffix++);
		}
		field->name = assetId;

		if (!_resourceManager->addField(std::move(field))) {
			_userStartupSelectionStatus = "Failed to register PLY asset.";
			return false;
		}

		if (auto* renderingSystem = getRenderingSystem()) {
			renderingSystem->setManifest(nullptr);
		}
		_manifestStore.clear();
		_loadedManifestPath.clear();
		_currentPhase.clear();

		std::string instanceName = assetId;
		suffix = 1;
		while (_scene->getInstance(instanceName) != nullptr) {
			instanceName = assetId + "_" + std::to_string(suffix++);
		}

		_selectedInstance = _scene->createInstance(instanceName, assetId);
		if (!_selectedInstance) {
			_userStartupSelectionStatus = "Failed to create PLY instance.";
			return false;
		}

		_subsystem[RENDERING_SYSTEM]->onInstanceCreated(*_selectedInstance);
		focusCameraOnInstanceCenter(*_selectedInstance);
		_resourceBrowserStatus = "PLY loaded: " + assetId;
		return true;
	}

	size_t ExtendedGaussianViewer::createManifestInstances(bool onlyMissing)
	{
		if (_manifestStore.empty() || !_scene) {
			return 0;
		}

		std::vector<std::string> assetIds;
		assetIds.reserve(_manifestStore.assets().size());
		for (const auto& assetPair : _manifestStore.assets()) {
			assetIds.push_back(assetPair.first);
		}
		std::sort(assetIds.begin(), assetIds.end());

		size_t createdCount = 0;
		for (const auto& assetId : assetIds) {
			if (onlyMissing && _scene->countInstancesUsingAsset(assetId) > 0) {
				continue;
			}

			std::string instanceName = assetId;
			int suffix = 1;
			while (_scene->getInstance(instanceName) != nullptr) {
				const GaussianInstance* existingInstance = _scene->getInstance(instanceName);
				if (existingInstance && existingInstance->getAssetId() == assetId) {
					instanceName.clear();
					break;
				}
				instanceName = assetId + "_" + std::to_string(suffix++);
			}

			if (instanceName.empty()) {
				continue;
			}

			GaussianInstance* instance = _scene->createInstance(instanceName, assetId);
			if (!instance) {
				continue;
			}

			_selectedInstance = instance;
			_subsystem[RENDERING_SYSTEM]->onInstanceCreated(*instance);
			++createdCount;
		}

		return createdCount;
	}

	bool ExtendedGaussianViewer::canFocusBlockCenter() const
	{
		Vector3f minBounds = Vector3f::Zero();
		Vector3f maxBounds = Vector3f::Zero();
		if (_selectedInstance && getInstanceWorldBounds(*_selectedInstance, minBounds, maxBounds)) {
			return true;
		}

		if (_resourceManager) {
			const auto assetIds = _resourceManager->listAssetIds();
			if (assetIds.size() == 1) {
				AssetDescriptor descriptor;
				if (_resourceManager->getAssetDescriptor(assetIds.front(), descriptor)) {
					return true;
				}
			}
		}

		return getManifestBounds(minBounds, maxBounds);
	}

	bool ExtendedGaussianViewer::focusCameraOnBlockCenter()
	{
		if (_selectedInstance && focusCameraOnInstanceCenter(*_selectedInstance)) {
			return true;
		}

		if (_resourceManager) {
			const auto assetIds = _resourceManager->listAssetIds();
			if (assetIds.size() == 1) {
				return focusCameraOnAssetCenter(assetIds.front());
			}
		}

		return focusCameraOnManifestBounds();
	}

	bool ExtendedGaussianViewer::focusCameraOnManifestBounds()
	{
		if (_manifestStore.empty()) {
			return false;
		}

		Vector3f minBounds = Vector3f::Zero();
		Vector3f maxBounds = Vector3f::Zero();
		if (!getManifestBounds(minBounds, maxBounds)) {
			return false;
		}

		return focusCameraOnBounds(minBounds, maxBounds);
	}

	bool ExtendedGaussianViewer::focusCameraOnBounds(const Vector3f& minBounds, const Vector3f& maxBounds)
	{
		const Vector3f center = 0.5f * (minBounds + maxBounds);
		return focusCameraOnTarget(center, minBounds, maxBounds);
	}

	bool ExtendedGaussianViewer::focusCameraOnTarget(const Vector3f& target, const Vector3f& minBounds, const Vector3f& maxBounds)
	{
		const auto viewIt = _ibrSubViews.find("Gaussian View");
		if (viewIt == _ibrSubViews.end()) {
			return false;
		}

		auto customHandler = std::dynamic_pointer_cast<CustomCameraHandler>(viewIt->second.handler);
		auto interactiveHandler = std::dynamic_pointer_cast<InteractiveCameraHandler>(viewIt->second.handler);
		if (!customHandler && !interactiveHandler) {
			return false;
		}

		const Vector3f diagonal = maxBounds - minBounds;
		const float maxExtent = std::max(1.0f, std::max(diagonal.x(), std::max(diagonal.y(), diagonal.z())));
		const float zMargin = std::max(0.1f, 0.05f * diagonal.z());
		const float preferredZOffset = std::max(1.0f, 0.25f * diagonal.z());
		const float clampedZ = std::min(maxBounds.z() - zMargin, target.z() + preferredZOffset);
		Vector3f eye = target;
		eye.z() = std::max(minBounds.z() + zMargin, clampedZ);

		// Keep the camera inside the focused AABB so camera_bounds rules immediately activate.
		const float xyInset = std::max(0.01f, 0.02f * maxExtent);
		eye.x() = std::min(maxBounds.x() - xyInset, std::max(minBounds.x() + xyInset, eye.x()));
		eye.y() = std::min(maxBounds.y() - xyInset, std::max(minBounds.y() + xyInset, eye.y()));

		InputCamera focusCamera = viewIt->second.cam;
		Vector3f cameraUp = _manifestStore.settings().initial_camera_up;
		if (cameraUp.squaredNorm() < 1e-6f) {
			cameraUp = Vector3f(0.0f, 1.0f, 0.0f);
		}
		focusCamera.setLookAt(eye, target, cameraUp.normalized());
		focusCamera.znear(0.01f);
		focusCamera.zfar(std::max(1000.0f, maxExtent * 20.0f));
		if (customHandler) {
			customHandler->fromCamera(focusCamera, false, true);
		}
		else {
			interactiveHandler->fromCamera(focusCamera, false, true);
		}
		viewIt->second.cam = focusCamera;
		return true;
	}

	bool ExtendedGaussianViewer::focusCameraOnAssetCenter(const AssetId& assetId)
	{
		AssetDescriptor descriptor;
		if (!resolveAssetFocusDescriptor(assetId, descriptor)) {
			return false;
		}

		const Vector3f minBounds = descriptor.has_focus_bounds ? descriptor.focus_bounds_min : descriptor.bounds_min;
		const Vector3f maxBounds = descriptor.has_focus_bounds ? descriptor.focus_bounds_max : descriptor.bounds_max;
		const Vector3f target = descriptor.has_camera_focus_center
			? descriptor.camera_focus_center
			: (descriptor.has_focus_bounds
				? descriptor.focus_center
				: (descriptor.has_gaussian_centroid
					? descriptor.gaussian_centroid
					: 0.5f * (minBounds + maxBounds)));
		return focusCameraOnTarget(target, minBounds, maxBounds);
	}

	bool ExtendedGaussianViewer::focusCameraOnInstanceCenter(const GaussianInstance& instance)
	{
		if (!instance.hasAsset()) {
			return false;
		}

		AssetDescriptor descriptor;
		if (!resolveAssetFocusDescriptor(instance.getAssetId(), descriptor)) {
			return false;
		}

		const Vector3f localMin = descriptor.has_focus_bounds ? descriptor.focus_bounds_min : descriptor.bounds_min;
		const Vector3f localMax = descriptor.has_focus_bounds ? descriptor.focus_bounds_max : descriptor.bounds_max;
		Vector3f minBounds = Vector3f::Zero();
		Vector3f maxBounds = Vector3f::Zero();
		if (!transformInstanceBounds(instance, localMin, localMax, minBounds, maxBounds)) {
			return false;
		}

		const Vector3f localTarget = descriptor.has_camera_focus_center
			? descriptor.camera_focus_center
			: (descriptor.has_focus_bounds
				? descriptor.focus_center
				: (descriptor.has_gaussian_centroid
					? descriptor.gaussian_centroid
					: 0.5f * (localMin + localMax)));
		const Matrix3f transform = instance.getRotationQuaternion().toRotationMatrix() * instance.getScale();
		const Vector3f worldTarget = transform * localTarget + instance.getPosition();
		return focusCameraOnTarget(worldTarget, minBounds, maxBounds);
	}

	bool ExtendedGaussianViewer::resolveAssetFocusDescriptor(const AssetId& assetId, AssetDescriptor& descriptor)
	{
		if (!_resourceManager || !_resourceManager->getAssetDescriptor(assetId, descriptor)) {
			return false;
		}

		if (descriptor.has_camera_focus_center) {
			return true;
		}

		const auto residentField = _resourceManager->getCpuFieldShared(assetId);
		if (residentField) {
			if (residentField->has_camera_focus_center) {
				descriptor.has_camera_focus_center = true;
				descriptor.camera_focus_center = residentField->camera_focus_center;
				return true;
			}
			if (residentField->has_centroid) {
				descriptor.has_gaussian_centroid = true;
				descriptor.gaussian_centroid = residentField->centroid;
			}
			if (residentField->has_focus_bounds) {
				descriptor.has_focus_bounds = true;
				descriptor.focus_center = residentField->focus_center;
				descriptor.focus_bounds_min = residentField->focus_bounds_min;
				descriptor.focus_bounds_max = residentField->focus_bounds_max;
				return true;
			}
		}

		if (descriptor.model_dir.empty()) {
			return true;
		}

		SIBR_LOG << "Loading asset '" << assetId << "' to compute Gaussian focus bounds." << std::endl;
		auto field = GaussianLoader::load(descriptor.model_dir.string());
		if (!field) {
			return true;
		}

		field->name = assetId;
		field->path = descriptor.model_dir.string();
		_resourceManager->addField(std::move(field));
		return _resourceManager->getAssetDescriptor(assetId, descriptor);
	}

	bool ExtendedGaussianViewer::getManifestBounds(Vector3f& minBounds, Vector3f& maxBounds) const
	{
		if (_manifestStore.empty()) {
			return false;
		}

		bool hasBounds = false;
		for (const auto& assetPair : _manifestStore.assets()) {
			const auto& descriptor = assetPair.second;
			if (!hasBounds) {
				minBounds = descriptor.bounds_min;
				maxBounds = descriptor.bounds_max;
				hasBounds = true;
				continue;
			}
			minBounds = minBounds.cwiseMin(descriptor.bounds_min);
			maxBounds = maxBounds.cwiseMax(descriptor.bounds_max);
		}

		if (!hasBounds) {
			return false;
		}

		return true;
	}

	bool ExtendedGaussianViewer::getInstanceWorldBounds(const GaussianInstance& instance, Vector3f& minBounds, Vector3f& maxBounds) const
	{
		if (!_resourceManager || !instance.hasAsset()) {
			return false;
		}

		AssetDescriptor descriptor;
		if (!_resourceManager->getAssetDescriptor(instance.getAssetId(), descriptor)) {
			return false;
		}

		return transformInstanceBounds(instance, descriptor.bounds_min, descriptor.bounds_max, minBounds, maxBounds);
	}

	bool ExtendedGaussianViewer::transformInstanceBounds(const GaussianInstance& instance, const Vector3f& localMin, const Vector3f& localMax, Vector3f& minBounds, Vector3f& maxBounds) const
	{
		const std::array<Vector3f, 8> corners = {
			Vector3f(localMin.x(), localMin.y(), localMin.z()),
			Vector3f(localMin.x(), localMin.y(), localMax.z()),
			Vector3f(localMin.x(), localMax.y(), localMin.z()),
			Vector3f(localMin.x(), localMax.y(), localMax.z()),
			Vector3f(localMax.x(), localMin.y(), localMin.z()),
			Vector3f(localMax.x(), localMin.y(), localMax.z()),
			Vector3f(localMax.x(), localMax.y(), localMin.z()),
			Vector3f(localMax.x(), localMax.y(), localMax.z())
		};

		const Matrix3f transform = instance.getRotationQuaternion().toRotationMatrix() * instance.getScale();
		bool hasBounds = false;
		for (const Vector3f& corner : corners) {
			const Vector3f worldCorner = transform * corner + instance.getPosition();
			if (!hasBounds) {
				minBounds = worldCorner;
				maxBounds = worldCorner;
				hasBounds = true;
				continue;
			}
			minBounds = minBounds.cwiseMin(worldCorner);
			maxBounds = maxBounds.cwiseMax(worldCorner);
		}

		return hasBounds;
	}

	const char* ExtendedGaussianViewer::cpuStateLabel(CpuState state)
	{
		switch (state) {
		case CpuState::Loading: return "Loading";
		case CpuState::Resident: return "CPU";
		case CpuState::EvictQueued: return "Evicting";
		case CpuState::Failed: return "Failed";
		case CpuState::Unloaded:
		default: return "Unloaded";
		}
	}

	const char* ExtendedGaussianViewer::gpuStateLabel(GpuState state)
	{
		switch (state) {
		case GpuState::UploadQueued: return "Uploading";
		case GpuState::Resident: return "GPU";
		case GpuState::EvictQueued: return "Evicting";
		case GpuState::Failed: return "Failed";
		case GpuState::Unloaded:
		default: return "Unloaded";
		}
	}

	std::string ExtendedGaussianViewer::formatMegabytes(size_t bytes)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / (1024.0 * 1024.0));
		return stream.str();
	}

	void ExtendedGaussianViewer::setMaxShDegree(int degree)
	{
		_maxShDegree = clampShDegree(degree);
		if (RenderingSystem* renderingSystem = getRenderingSystem()) {
			renderingSystem->setMaxShDegree(_maxShDegree);
		}
	}

	std::vector<ExtendedGaussianViewer::UserMapBlock> ExtendedGaussianViewer::collectUserMapBlocks() const
	{
		std::unordered_set<AssetId> residentAssetIds;
		for (const auto& status : GPUResourceManager::getInstance().snapshot()) {
			if (status.resident) {
				residentAssetIds.insert(status.id);
			}
		}

		std::vector<UserMapBlock> blocks;
		std::unordered_set<AssetId> instanceAssetIds;

		if (_scene) {
			std::vector<const GaussianInstance*> instances;
			for (const auto& pair : _scene->getInstances()) {
				if (pair.second) {
					instances.push_back(pair.second.get());
				}
			}
			std::sort(instances.begin(), instances.end(), [](const GaussianInstance* lhs, const GaussianInstance* rhs) {
				return lhs->getName() < rhs->getName();
			});

			for (const GaussianInstance* instance : instances) {
				if (!instance || !instance->hasAsset()) {
					continue;
				}

				Vector3f minBounds = Vector3f::Zero();
				Vector3f maxBounds = Vector3f::Zero();
				if (!getInstanceWorldBounds(*instance, minBounds, maxBounds)) {
					continue;
				}

				UserMapBlock block;
				block.id = instance->getName();
				block.minBounds = minBounds;
				block.maxBounds = maxBounds;
				block.gpuResident = residentAssetIds.count(instance->getAssetId()) > 0;
				block.hasInstance = true;
				blocks.push_back(block);
				instanceAssetIds.insert(instance->getAssetId());
			}
		}

		if (_resourceManager) {
			auto assetIds = _resourceManager->listAssetIds();
			std::sort(assetIds.begin(), assetIds.end());
			for (const auto& assetId : assetIds) {
				if (instanceAssetIds.count(assetId) > 0) {
					continue;
				}

				AssetDescriptor descriptor;
				if (!_resourceManager->getAssetDescriptor(assetId, descriptor)) {
					continue;
				}

				UserMapBlock block;
				block.id = assetId;
				block.minBounds = descriptor.bounds_min;
				block.maxBounds = descriptor.bounds_max;
				block.gpuResident = residentAssetIds.count(assetId) > 0;
				block.hasInstance = false;
				blocks.push_back(block);
			}
		}

		return blocks;
	}

	bool ExtendedGaussianViewer::focusCameraOnUserBlock(const std::string& id)
	{
		if (id.empty()) {
			return false;
		}

		if (_scene) {
			if (GaussianInstance* instance = _scene->getInstance(id)) {
				_selectedInstance = instance;
				return focusCameraOnInstanceCenter(*instance);
			}

			for (const auto& pair : _scene->getInstances()) {
				GaussianInstance* instance = pair.second.get();
				if (instance && instance->getAssetId() == id) {
					_selectedInstance = instance;
					return focusCameraOnInstanceCenter(*instance);
				}
			}
		}

		return focusCameraOnAssetCenter(id);
	}

	void ExtendedGaussianViewer::onShowUserStartupSelection(Window& win)
	{
		const UserUILayout layout = getUserUILayout(win);
		const float scale = layout.scale;
		const float modalWidth = fitUserDimension(420.0f * scale, 300.0f * layout.dpiScale, layout.winSize.x() - 2.0f * layout.margin);
		const float modalHeight = fitUserDimension(220.0f * scale, 190.0f * layout.dpiScale, layout.winSize.y() - 2.0f * layout.margin);
		const ImVec2 modalSize(modalWidth, modalHeight);
		ImGui::SetNextWindowPos(ImVec2(layout.winSize.x() * 0.5f, layout.winSize.y() * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
		ImGui::OpenPopup("데이터 선택");

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale, 16.0f * scale));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * scale);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1.00f, 1.00f, 1.00f, 0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.73f, 0.78f, 0.86f, 0.80f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.09f, 0.16f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.56f, 0.79f, 0.98f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.84f, 0.99f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.49f, 0.73f, 0.95f, 1.00f));

		if (ImGui::BeginPopupModal("데이터 선택", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
			ImGui::SetWindowFontScale(layout.resolutionScale);
			ImGui::TextUnformatted("시작할 데이터를 선택하세요");
			ImGui::TextDisabled("manifest JSON 또는 단일 PLY 파일을 불러옵니다.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button("Manifest 선택", ImVec2(-1.0f, 34.0f * scale))) {
				std::string manifestPath;
				if (showFilePicker(manifestPath, FilePickerMode::Default, "", "json")) {
					if (loadManifestFile(manifestPath)) {
						_userStartupSelectionOpen = false;
						_userStartupSelectionStatus.clear();
						ImGui::CloseCurrentPopup();
					}
					else {
						_userStartupSelectionStatus = "Manifest load failed.";
					}
				}
			}

			if (ImGui::Button("PLY 선택", ImVec2(-1.0f, 34.0f * scale))) {
				std::string plyPath;
				if (showFilePicker(plyPath, FilePickerMode::Default, "", "ply")) {
					if (importUserPlyFile(plyPath)) {
						_userStartupSelectionOpen = false;
						_userStartupSelectionStatus.clear();
						ImGui::CloseCurrentPopup();
					}
				}
			}

			if (!_userStartupSelectionStatus.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.82f, 0.18f, 0.15f, 1.00f), "%s", _userStartupSelectionStatus.c_str());
			}

			ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 42.0f * scale);
			if (ImGui::Button("나중에 선택", ImVec2(-1.0f, 0.0f))) {
				_userStartupSelectionOpen = false;
				_userStartupSelectionStatus.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::PopStyleColor(6);
		ImGui::PopStyleVar(3);
	}

	void ExtendedGaussianViewer::onShowUserMinimap(Window& win)
	{
		const UserUILayout layout = getUserUILayout(win);
		const float scale = layout.scale;
		const bool fixedRoomMap = _userMinimapMode == UserMinimapMode::FixedRooms;
		const float controlWidth = 86.0f * scale;
		const float minMapWidth = 220.0f * layout.dpiScale;
		const float maxMapWidth = std::max(minMapWidth, layout.winSize.x() - controlWidth - 3.0f * layout.margin);
		const float maxMapHeight = std::max(140.0f * layout.dpiScale, layout.winSize.y() - 2.0f * layout.margin);

		float mapWidth = 0.0f;
		float mapHeight = 0.0f;
		if (fixedRoomMap) {
			mapWidth = fitUserDimension((_userMinimapExpanded ? 584.0f : 300.0f) * scale, minMapWidth, maxMapWidth);
			mapHeight = fitUserDimension(170.0f * scale, 120.0f * layout.dpiScale, maxMapHeight);
		}
		else {
			const float compactMapWidth = 320.0f * scale;
			const float compactMapHeight = 180.0f * scale;
			const float expandedMapWidth = std::min(std::max(420.0f * scale, layout.winSize.x() - 96.0f * scale), 760.0f * scale);
			const float expandedMapHeight = std::min(std::max(260.0f * scale, layout.winSize.y() * 0.52f), 520.0f * scale);
			mapWidth = fitUserDimension(_userMinimapExpanded ? expandedMapWidth : compactMapWidth, minMapWidth, maxMapWidth);
			mapHeight = fitUserDimension(_userMinimapExpanded ? expandedMapHeight : compactMapHeight, 130.0f * layout.dpiScale, maxMapHeight);
		}

		const float windowWidth = mapWidth + controlWidth + 34.0f * scale;
		const float windowHeight = std::max(mapHeight + 20.0f * scale, 102.0f * scale);

		ImGui::SetNextWindowPos(ImVec2(layout.margin, layout.margin), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 10.0f * scale));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.97f, 0.98f, 0.99f, 0.86f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.73f, 0.78f, 0.86f, 0.70f));

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoSavedSettings;

		if (ImGui::Begin("User Minimap", nullptr, flags)) {
			ImGui::SetWindowFontScale(layout.resolutionScale);
			const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
			const ImVec2 canvasSize(mapWidth, mapHeight);
			ImGui::InvisibleButton("##UserMinimapCanvas", canvasSize);
			const bool canvasHovered = ImGui::IsItemHovered();
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(248, 250, 252, 235), 6.0f * scale);
			drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(148, 163, 184, 180), 6.0f * scale, 0, 1.0f * scale);

			std::string clickedBlock;
			std::string clickedBlockFallback;
			if (fixedRoomMap) {
				struct FixedRoomBlock {
					const char* id;
					const char* label;
					float x;
					float y;
					float w;
					float h;
					bool office;
					float textScale;
				};
				static const std::array<FixedRoomBlock, 16> fixedBlocks = { {
					{ "401", "401", 0.0f, 0.0f, 84.0f, 58.0f, false, 1.0f },
					{ "지란지교", "지란지교", 84.0f, 0.0f, 70.0f, 58.0f, false, 0.82f },
					{ "404-1", "404", 154.0f, 0.0f, 70.0f, 58.0f, false, 1.0f },
					{ "405", "405", 224.0f, 0.0f, 62.0f, 58.0f, false, 1.0f },
					{ "406", "406", 350.0f, 0.0f, 76.0f, 45.0f, true, 1.0f },
					{ "407", "407", 426.0f, 0.0f, 36.0f, 45.0f, true, 0.9f },
					{ "사무실/부속실", "사무실/부속실", 462.0f, 0.0f, 58.0f, 45.0f, true, 0.58f },
					{ "408", "408", 520.0f, 0.0f, 40.0f, 45.0f, true, 0.9f },
					{ "416", "416", 0.0f, 88.0f, 78.0f, 62.0f, false, 1.0f },
					{ "415", "415", 78.0f, 88.0f, 78.0f, 62.0f, false, 1.0f },
					{ "414", "414", 156.0f, 88.0f, 78.0f, 62.0f, false, 1.0f },
					{ "413", "413", 234.0f, 88.0f, 66.0f, 62.0f, false, 1.0f },
					{ "412", "412", 300.0f, 88.0f, 62.0f, 62.0f, false, 1.0f },
					{ "411", "411", 362.0f, 88.0f, 68.0f, 62.0f, false, 1.0f },
					{ "410", "410", 430.0f, 88.0f, 82.0f, 62.0f, false, 1.0f },
					{ "409", "409", 512.0f, 88.0f, 48.0f, 62.0f, false, 1.0f },
				} };

				const std::string selectedName = _selectedInstance ? _selectedInstance->getName() : _userFixedMinimapSelectedId;
				const FixedRoomBlock* selectedBlock = nullptr;
				for (const FixedRoomBlock& block : fixedBlocks) {
					if (selectedName == block.id || selectedName == block.label) {
						selectedBlock = &block;
						break;
					}
				}
				if (!selectedBlock) {
					for (const FixedRoomBlock& block : fixedBlocks) {
						if (_userFixedMinimapSelectedId == block.id || _userFixedMinimapSelectedId == block.label) {
							selectedBlock = &block;
							break;
						}
					}
				}
				if (!selectedBlock) {
					selectedBlock = &fixedBlocks.front();
				}

				const ImVec2 selectedRoomCenter(
					selectedBlock->x + selectedBlock->w * 0.5f,
					selectedBlock->y + selectedBlock->h * 0.5f);
				const ImVec2 mapCenter(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
				float planScale = std::min(scale, std::max(0.1f, (canvasSize.y - 20.0f * layout.dpiScale) / 150.0f));
				if (_userMinimapExpanded) {
					planScale = std::min(planScale, std::max(0.1f, (canvasSize.x - 24.0f * layout.dpiScale) / 560.0f));
				}
				const ImVec2 planMin = _userMinimapExpanded
					? ImVec2(canvasPos.x + (canvasSize.x - 560.0f * planScale) * 0.5f, canvasPos.y + (canvasSize.y - 150.0f * planScale) * 0.5f)
					: ImVec2(mapCenter.x - selectedRoomCenter.x * planScale, mapCenter.y - selectedRoomCenter.y * planScale);

				const ImU32 roomFill = IM_COL32(229, 231, 235, 205);
				const ImU32 activeRoomFill = IM_COL32(255, 255, 255, 245);
				const ImU32 officeFill = IM_COL32(250, 232, 255, 210);
				const ImU32 roomBorder = IM_COL32(100, 116, 139, 210);
				const ImU32 activeBorder = IM_COL32(143, 202, 249, 255);
				const ImU32 accentColor = IM_COL32(56, 189, 248, 255);
				const ImU32 roomText = IM_COL32(15, 23, 42, 230);

				drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
				const bool corridorHovered =
					canvasHovered &&
					(((mousePos.x >= planMin.x + 0.0f * planScale && mousePos.x <= planMin.x + 560.0f * planScale) && (mousePos.y >= planMin.y + 58.0f * planScale && mousePos.y <= planMin.y + 88.0f * planScale)) ||
						((mousePos.x >= planMin.x + 286.0f * planScale && mousePos.x <= planMin.x + 350.0f * planScale) && (mousePos.y >= planMin.y + 0.0f * planScale && mousePos.y <= planMin.y + 88.0f * planScale)) ||
						((mousePos.x >= planMin.x + 350.0f * planScale && mousePos.x <= planMin.x + 560.0f * planScale) && (mousePos.y >= planMin.y + 45.0f * planScale && mousePos.y <= planMin.y + 88.0f * planScale)));
				drawList->AddRectFilled(ImVec2(planMin.x + 0.0f * planScale, planMin.y + 58.0f * planScale), ImVec2(planMin.x + 560.0f * planScale, planMin.y + 88.0f * planScale), roomFill, 2.0f * planScale);
				drawList->AddRectFilled(ImVec2(planMin.x + 286.0f * planScale, planMin.y + 0.0f * planScale), ImVec2(planMin.x + 350.0f * planScale, planMin.y + 88.0f * planScale), roomFill, 2.0f * planScale);
				drawList->AddRectFilled(ImVec2(planMin.x + 350.0f * planScale, planMin.y + 45.0f * planScale), ImVec2(planMin.x + 560.0f * planScale, planMin.y + 88.0f * planScale), roomFill, 2.0f * planScale);
				ImVec2 corridorOutline[] = {
					ImVec2(planMin.x + 0.0f * planScale, planMin.y + 58.0f * planScale),
					ImVec2(planMin.x + 286.0f * planScale, planMin.y + 58.0f * planScale),
					ImVec2(planMin.x + 286.0f * planScale, planMin.y + 0.0f * planScale),
					ImVec2(planMin.x + 350.0f * planScale, planMin.y + 0.0f * planScale),
					ImVec2(planMin.x + 350.0f * planScale, planMin.y + 45.0f * planScale),
					ImVec2(planMin.x + 560.0f * planScale, planMin.y + 45.0f * planScale),
					ImVec2(planMin.x + 560.0f * planScale, planMin.y + 88.0f * planScale),
					ImVec2(planMin.x + 0.0f * planScale, planMin.y + 88.0f * planScale),
				};
				drawList->AddPolyline(corridorOutline, IM_ARRAYSIZE(corridorOutline), corridorHovered ? accentColor : roomBorder, true, 1.0f * planScale);

				for (const FixedRoomBlock& block : fixedBlocks) {
					const ImVec2 a(planMin.x + block.x * planScale, planMin.y + block.y * planScale);
					const ImVec2 b(planMin.x + (block.x + block.w) * planScale, planMin.y + (block.y + block.h) * planScale);
					const bool hovered = canvasHovered
						&& mousePos.x >= a.x && mousePos.x <= b.x
						&& mousePos.y >= a.y && mousePos.y <= b.y;
					if (hovered && ImGui::IsMouseClicked(0)) {
						clickedBlock = block.id;
						clickedBlockFallback = block.label;
					}

					const bool selected = selectedName == block.id || selectedName == block.label;
					const ImU32 fill = selected ? activeRoomFill : (block.office ? officeFill : roomFill);
					const ImU32 border = selected ? activeBorder : (hovered ? accentColor : roomBorder);
					drawList->AddRectFilled(a, b, fill, 2.0f * planScale);
					drawList->AddRect(a, b, border, 2.0f * planScale, 0, selected ? 2.0f * planScale : 1.0f * planScale);

					const float labelScale = block.textScale * std::min(1.0f, planScale / std::max(0.001f, scale));
					ImVec2 textSize = ImGui::CalcTextSize(block.label);
					textSize.x *= labelScale;
					textSize.y *= labelScale;
					const ImVec2 textPos((a.x + b.x - textSize.x) * 0.5f, (a.y + b.y - textSize.y) * 0.5f);
					if (labelScale != 1.0f) {
						drawList->AddText(nullptr, ImGui::GetFontSize() * labelScale, textPos, roomText, block.label);
					}
					else {
						drawList->AddText(textPos, roomText, block.label);
					}
				}
				drawList->PopClipRect();
			}
			else {
				const std::vector<UserMapBlock> allBlocks = collectUserMapBlocks();
				std::vector<const UserMapBlock*> visibleBlocks;
				for (const auto& block : allBlocks) {
					const bool selected = _selectedInstance && block.id == _selectedInstance->getName();
					if (_userMinimapExpanded || block.gpuResident || selected) {
						visibleBlocks.push_back(&block);
					}
				}
				if (visibleBlocks.empty()) {
					for (const auto& block : allBlocks) {
						visibleBlocks.push_back(&block);
					}
				}

				if (visibleBlocks.empty()) {
					const char* emptyText = "No blocks";
					const ImVec2 textSize = ImGui::CalcTextSize(emptyText);
					drawList->AddText(
						ImVec2(canvasPos.x + (canvasSize.x - textSize.x) * 0.5f, canvasPos.y + (canvasSize.y - textSize.y) * 0.5f),
						IM_COL32(100, 116, 139, 255),
						emptyText);
				}
				else {
					float minX = std::numeric_limits<float>::max();
					float minY = std::numeric_limits<float>::max();
					float maxX = -std::numeric_limits<float>::max();
					float maxY = -std::numeric_limits<float>::max();
					for (const UserMapBlock* block : visibleBlocks) {
						minX = std::min(minX, block->minBounds.x());
						minY = std::min(minY, block->minBounds.y());
						maxX = std::max(maxX, block->maxBounds.x());
						maxY = std::max(maxY, block->maxBounds.y());
					}

					const float extentX = std::max(1.0f, maxX - minX);
					const float extentY = std::max(1.0f, maxY - minY);
					const float worldPadding = 0.08f * std::max(extentX, extentY);
					minX -= worldPadding;
					minY -= worldPadding;
					maxX += worldPadding;
					maxY += worldPadding;

					const float usableWidth = std::max(1.0f, canvasSize.x - 16.0f * scale);
					const float usableHeight = std::max(1.0f, canvasSize.y - 16.0f * scale);
					const float worldWidth = std::max(1.0f, maxX - minX);
					const float worldHeight = std::max(1.0f, maxY - minY);
					const float mapScale = std::min(usableWidth / worldWidth, usableHeight / worldHeight);
					const float offsetX = (canvasSize.x - worldWidth * mapScale) * 0.5f;
					const float offsetY = (canvasSize.y - worldHeight * mapScale) * 0.5f;

					auto worldToScreen = [&](float x, float y) {
						return ImVec2(
							canvasPos.x + offsetX + (x - minX) * mapScale,
							canvasPos.y + canvasSize.y - offsetY - (y - minY) * mapScale);
					};

					drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
					for (const UserMapBlock* block : visibleBlocks) {
						ImVec2 a = worldToScreen(block->minBounds.x(), block->maxBounds.y());
						ImVec2 b = worldToScreen(block->maxBounds.x(), block->minBounds.y());
						if (b.x - a.x < 22.0f * scale) {
							const float center = (a.x + b.x) * 0.5f;
							a.x = center - 11.0f * scale;
							b.x = center + 11.0f * scale;
						}
						if (b.y - a.y < 18.0f * scale) {
							const float center = (a.y + b.y) * 0.5f;
							a.y = center - 9.0f * scale;
							b.y = center + 9.0f * scale;
						}

						const bool selected = _selectedInstance && block->id == _selectedInstance->getName();
						const bool hovered = canvasHovered
							&& mousePos.x >= a.x && mousePos.x <= b.x
							&& mousePos.y >= a.y && mousePos.y <= b.y;
						if (hovered && ImGui::IsMouseClicked(0)) {
							clickedBlock = block->id;
						}

						const ImU32 fill = selected
							? IM_COL32(255, 255, 255, 245)
							: (block->gpuResident ? IM_COL32(219, 242, 255, 235) : IM_COL32(229, 231, 235, 215));
						const ImU32 border = selected
							? IM_COL32(14, 165, 233, 255)
							: (hovered ? IM_COL32(56, 189, 248, 255) : IM_COL32(100, 116, 139, 210));
						drawList->AddRectFilled(a, b, fill, 3.0f * scale);
						drawList->AddRect(a, b, border, 3.0f * scale, 0, selected ? 2.0f * scale : 1.0f * scale);

						const ImVec2 textSize = ImGui::CalcTextSize(block->id.c_str());
						if (textSize.x + 8.0f * scale < b.x - a.x && textSize.y < b.y - a.y) {
							drawList->AddText(
								ImVec2(a.x + (b.x - a.x - textSize.x) * 0.5f, a.y + (b.y - a.y - textSize.y) * 0.5f),
								IM_COL32(15, 23, 42, 230),
								block->id.c_str());
						}
					}
					drawList->PopClipRect();
				}
			}

			if (!clickedBlock.empty()) {
				if (fixedRoomMap) {
					_userFixedMinimapSelectedId = clickedBlock;
					if (!focusCameraOnUserBlock(clickedBlock) && clickedBlockFallback != clickedBlock) {
						focusCameraOnUserBlock(clickedBlockFallback);
					}
				}
				else {
					focusCameraOnUserBlock(clickedBlock);
				}
			}

			ImGui::SameLine(0.0f, 8.0f * scale);
			ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.56f, 0.79f, 0.98f, 0.95f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.84f, 0.99f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.49f, 0.73f, 0.95f, 1.00f));
			if (ImGui::Button(_userMinimapExpanded ? "Focus" : "Full", ImVec2(68.0f * scale, 0.0f))) {
				_userMinimapExpanded = !_userMinimapExpanded;
			}
			if (ImGui::Button(fixedRoomMap ? "입력" : "고정", ImVec2(68.0f * scale, 0.0f))) {
				_userMinimapMode = fixedRoomMap ? UserMinimapMode::InputBlocks : UserMinimapMode::FixedRooms;
			}
			ImGui::PopStyleColor(3);
			ImGui::TextDisabled("%s", fixedRoomMap ? "고정 지도" : (_currentPhase.empty() ? "입력 지도" : _currentPhase.c_str()));
			ImGui::EndGroup();
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	void ExtendedGaussianViewer::onShowUserInstanceList(Window& win)
	{
		if (!_scene) {
			return;
		}

		const UserUILayout layout = getUserUILayout(win);
		const float scale = layout.scale;
		const float maxPanelWidth = std::max(190.0f * layout.dpiScale, layout.winSize.x() * 0.32f);
		const float panelWidth = clampFloat(260.0f * scale, 190.0f * layout.dpiScale, maxPanelWidth);
		const float panelHeight = fitUserDimension(520.0f * scale, 280.0f * layout.dpiScale, layout.winSize.y() - 2.0f * layout.margin);
		const ImVec2 panelPos(
			std::max(layout.margin, layout.winSize.x() - panelWidth - layout.margin),
			layout.margin);

		ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 10.0f * scale));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.00f, 1.00f, 1.00f, 0.92f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.73f, 0.78f, 0.86f, 0.72f));

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings;

		if (ImGui::Begin("User Instance List", nullptr, flags)) {
			ImGui::SetWindowFontScale(layout.resolutionScale);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.56f, 0.79f, 0.98f, 0.95f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.84f, 0.99f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.49f, 0.73f, 0.95f, 1.00f));
			ImGui::Button("즐겨찾기 목록", ImVec2(-1.0f, 0.0f));
			ImGui::PopStyleColor(3);

			std::vector<GaussianInstance*> instances;
			std::unordered_set<std::string> existingNames;
			for (const auto& pair : _scene->getInstances()) {
				if (pair.second) {
					instances.push_back(pair.second.get());
					existingNames.insert(pair.first);
				}
			}

			for (auto it = _userFavoriteInstances.begin(); it != _userFavoriteInstances.end();) {
				if (existingNames.count(*it) == 0) {
					it = _userFavoriteInstances.erase(it);
				}
				else {
					++it;
				}
			}

			std::sort(instances.begin(), instances.end(), [this](const GaussianInstance* lhs, const GaussianInstance* rhs) {
				const bool lhsFavorite = _userFavoriteInstances.count(lhs->getName()) > 0;
				const bool rhsFavorite = _userFavoriteInstances.count(rhs->getName()) > 0;
				if (lhsFavorite != rhsFavorite) {
					return lhsFavorite;
				}
				return lhs->getName() < rhs->getName();
			});

			const float listHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
			if (ImGui::BeginChild("UserInstanceListScroll", ImVec2(0.0f, listHeight), false)) {
				if (instances.empty()) {
					ImGui::TextDisabled("No instances");
				}
				for (GaussianInstance* instance : instances) {
					if (!instance) {
						continue;
					}

					const std::string& name = instance->getName();
					const bool favorite = _userFavoriteInstances.count(name) > 0;
					const bool selected = _selectedInstance == instance;
					ImGui::PushID(name.c_str());
					ImGui::PushStyleColor(ImGuiCol_Text, favorite ? ImVec4(0.96f, 0.62f, 0.04f, 1.0f) : ImVec4(0.39f, 0.45f, 0.55f, 1.0f));
					if (ImGui::Button(favorite ? "★" : "☆", ImVec2(30.0f * scale, 0.0f))) {
						if (favorite) {
							_userFavoriteInstances.erase(name);
						}
						else {
							_userFavoriteInstances.insert(name);
						}
					}
					ImGui::PopStyleColor();
					ImGui::SameLine();

					if (selected) {
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.86f, 0.95f, 1.0f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.82f, 0.93f, 1.0f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.76f, 0.89f, 1.0f, 1.0f));
					}
					if (ImGui::Selectable(name.c_str(), selected, 0, ImVec2(0.0f, 30.0f * scale))) {
						_selectedInstance = instance;
						focusCameraOnInstanceCenter(*instance);
					}
					if (selected) {
						ImGui::PopStyleColor(3);
					}
					ImGui::PopID();
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	void ExtendedGaussianViewer::setUserCameraMoveVector(const Vector2f& moveVector)
	{
		const auto viewIt = _ibrSubViews.find("Gaussian View");
		if (viewIt == _ibrSubViews.end()) {
			return;
		}

		auto customHandler = std::dynamic_pointer_cast<CustomCameraHandler>(viewIt->second.handler);
		if (customHandler) {
			customHandler->setUiMoveVector(moveVector);
		}
	}

	void ExtendedGaussianViewer::onShowUserNavigationControl(Window& win)
	{
		const UserUILayout layout = getUserUILayout(win);
		const float scale = layout.scale;
		const float panelSize = 178.0f * scale;
		const ImVec2 panelPos(layout.margin, std::max(layout.margin, layout.winSize.y() - panelSize - layout.margin));

		ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(panelSize, panelSize), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, panelSize * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.09f, 0.16f, 0.82f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.06f, 0.65f, 0.91f, 0.95f));

		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoSavedSettings;

		Vector2f moveVector(0.0f, 0.0f);
		if (ImGui::Begin("User Navigation Control", nullptr, flags)) {
			ImGui::SetWindowFontScale(layout.resolutionScale);
			const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
			const ImVec2 canvasSize(panelSize, panelSize);
			ImGui::InvisibleButton("##UserNavigationDpad", canvasSize);
			const bool active = ImGui::IsItemActive() && ImGui::IsMouseDown(0);
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			const ImVec2 center(canvasPos.x + panelSize * 0.5f, canvasPos.y + panelSize * 0.5f);
			const float radius = panelSize * 0.42f;

			if (active) {
				const float dx = (mousePos.x - center.x) / radius;
				const float dy = (mousePos.y - center.y) / radius;
				moveVector.x() = std::max(-1.0f, std::min(1.0f, dx));
				moveVector.y() = std::max(-1.0f, std::min(1.0f, -dy));
				const float length = moveVector.norm();
				if (length > 1.0f) {
					moveVector /= length;
				}
				if (moveVector.norm() < 0.15f) {
					moveVector = Vector2f(0.0f, 0.0f);
				}
			}

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddCircleFilled(center, radius, IM_COL32(15, 23, 42, 218), 64);
			drawList->AddCircle(center, radius, IM_COL32(14, 165, 233, 255), 64, 2.0f * scale);

			const float keyWidth = 38.0f * scale;
			const float keyHeight = 34.0f * scale;
			const float keyOffset = 43.0f * scale;
			const float keyRounding = 3.0f * scale;
			const ImU32 keyFill = IM_COL32(255, 255, 255, 235);
			const ImU32 keyActiveFill = IM_COL32(219, 242, 255, 245);
			const ImU32 keyBorder = IM_COL32(226, 232, 240, 245);
			const ImU32 keyActiveBorder = IM_COL32(56, 189, 248, 255);
			const ImU32 keyText = IM_COL32(15, 23, 42, 255);

			auto drawKey = [&](const char* label, const ImVec2& keyCenter, bool keyActive) {
				const ImVec2 a(keyCenter.x - keyWidth * 0.5f, keyCenter.y - keyHeight * 0.5f);
				const ImVec2 b(keyCenter.x + keyWidth * 0.5f, keyCenter.y + keyHeight * 0.5f);
				drawList->AddRectFilled(a, b, keyActive ? keyActiveFill : keyFill, keyRounding);
				drawList->AddRect(a, b, keyActive ? keyActiveBorder : keyBorder, keyRounding, 0, keyActive ? 1.8f * scale : 1.0f * scale);
				const ImVec2 textSize = ImGui::CalcTextSize(label);
				drawList->AddText(ImVec2(keyCenter.x - textSize.x * 0.5f, keyCenter.y - textSize.y * 0.5f), keyText, label);
			};

			drawKey("W", ImVec2(center.x, center.y - keyOffset), moveVector.y() > 0.25f);
			drawKey("D", ImVec2(center.x + keyOffset, center.y), moveVector.x() > 0.25f);
			drawKey("S", ImVec2(center.x, center.y + keyOffset), moveVector.y() < -0.25f);
			drawKey("A", ImVec2(center.x - keyOffset, center.y), moveVector.x() < -0.25f);

			const float gap = 9.0f * scale;
			drawList->AddQuadFilled(
				ImVec2(center.x, center.y - gap),
				ImVec2(center.x + gap, center.y),
				ImVec2(center.x, center.y + gap),
				ImVec2(center.x - gap, center.y),
				IM_COL32(15, 23, 42, 235));
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);

		setUserCameraMoveVector(moveVector);
	}

	void ExtendedGaussianViewer::onShowScenePanel(Window& win) {
		float sideWidth = 350.0f;
		ImGui::SetNextWindowPos(ImVec2(win.size().x() - sideWidth, 20.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(sideWidth, win.size().y() - 20.0f), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Scene Outliner", &_showScenePanel, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
			char phaseBuffer[128] = {};
			std::snprintf(phaseBuffer, sizeof(phaseBuffer), "%s", _currentPhase.c_str());
			if (ImGui::InputText("Current Phase", phaseBuffer, IM_ARRAYSIZE(phaseBuffer))) {
				_currentPhase = phaseBuffer;
			}

			size_t manifestInstanceCount = 0;
			for (const auto& assetPair : _manifestStore.assets()) {
				if (_scene->countInstancesUsingAsset(assetPair.first) > 0) {
					++manifestInstanceCount;
				}
			}

			if (!_manifestStore.empty()) {
				ImGui::Text("Manifest Instances: %u / %u",
					static_cast<unsigned>(manifestInstanceCount),
					static_cast<unsigned>(_manifestStore.assets().size()));
				if (ImGui::Button("Create Manifest Instances", ImVec2(-1, 25))) {
					const size_t createdCount = createManifestInstances(true);
					SIBR_LOG << "Created " << createdCount << " additional manifest instance(s)." << std::endl;
				}
			}
			const bool canFocus = canFocusBlockCenter();
			if (!canFocus) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}
			if (ImGui::Button("Focus Block Center", ImVec2(-1, 25)) && canFocus) {
				focusCameraOnBlockCenter();
			}
			if (!canFocus) {
				ImGui::PopStyleVar();
			}

			// Instance Creatttion Button
			if (ImGui::Button("Create New Instance", ImVec2(-1, 25))) {
				ImGui::OpenPopup("Create New Instance");
			}

			// Instance Creation Popup
			if (ImGui::BeginPopupModal("Create New Instance", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				static char nameBuf[128] = "NewInstance";
				static std::string previewAssetId;
				static Vector3f tempPos(0, 0, 0), tempRot(0, 0, 0);
				static float tempScale = 1.0f;

				ImGui::Text("Enter Instance Settings");
				ImGui::Separator();
				ImGui::Spacing();

				ImGui::InputText("Name", nameBuf, IM_ARRAYSIZE(nameBuf));

				std::string fieldName = previewAssetId.empty() ? "None" : previewAssetId;
				if (ImGui::BeginCombo("Gaussian Asset", fieldName.c_str())) {
					if (ImGui::Selectable("None", previewAssetId.empty())) {
						previewAssetId.clear();
					}

					for (const auto& assetId : _resourceManager->listAssetIds()) {
						if (ImGui::Selectable(assetId.c_str(), assetId == previewAssetId)) {
							previewAssetId = assetId;
						}
					}
					ImGui::EndCombo();
				}

				ImGui::Spacing();
				ImGui::Text("Initial Transform");
				ImGui::DragFloat3("Position", tempPos.data(), 0.1f);
				ImGui::DragFloat3("Rotation", tempRot.data(), 1.0f);
				ImGui::DragFloat("Scale", &tempScale, 0.01f, 0.001f, 100.0f);

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Create", ImVec2(120, 0))) {
					_selectedInstance = _scene->createInstance(nameBuf, previewAssetId, tempPos, tempRot, tempScale);
					if (_selectedInstance) {
						_subsystem[RENDERING_SYSTEM]->onInstanceCreated(*_selectedInstance);

						SIBR_LOG << "Instance created: " << nameBuf << (previewAssetId.empty() ? " (No Asset)" : "") << std::endl;
						ImGui::CloseCurrentPopup();

						// Reset buffers
						strcpy(nameBuf, "NewInstance");
						previewAssetId.clear();
						tempPos = Vector3f(0, 0, 0);
						tempRot = Vector3f(0, 0, 0);
						tempScale = 1.0f;
					}
					else {
						SIBR_WRG << "Failed to create instance. Check for duplicate name: " << nameBuf << std::endl;
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			const bool outlinerOpen = ImGui::BeginChild("OutlinerList", ImVec2(0, 250), true);
			if (outlinerOpen) {
				auto& allInstances = _scene->getInstances();
				for (auto& pair : allInstances) {
					GaussianInstance* inst = pair.second.get();
					bool isSelected = (_selectedInstance == inst);
					if (ImGui::Selectable(pair.first.c_str(), isSelected)) {
						_selectedInstance = inst;
					}
				}
			}
			ImGui::EndChild();

			ImGui::Spacing();
			ImGui::Separator();

			// DETAILS
			if (_selectedInstance) {
				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "DETAILS: %s", _selectedInstance->getNameRef().c_str());

				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
					// 1. Position
					if (ImGui::DragFloat3("Location", _selectedInstance->getPositionRef().data(), 0.1f)) {
					}

					// 2. Rotation
					if (ImGui::DragFloat3("Rotation", _selectedInstance->getEulerRef().data(), 0.5f)) {
					}

					// 3. Scale
					if (ImGui::DragFloat("Scale", &_selectedInstance->getScaleRef(), 0.01f, 0.001f, 100.0f)) {
					}
				}

				if (ImGui::CollapsingHeader("Gaussian Asset", ImGuiTreeNodeFlags_DefaultOpen)) {
					const std::string& currentAssetId = _selectedInstance->getAssetId();
					const auto currentField = _resourceManager->getCpuFieldShared(currentAssetId);
					std::string currentFieldName = currentAssetId.empty() ? "None" : currentAssetId;
					if (!currentField && !currentAssetId.empty()) {
						currentFieldName += " (missing)";
					}

					if (ImGui::BeginCombo("Source Field", currentFieldName.c_str())) {
						if (ImGui::Selectable("None", currentAssetId.empty())) {
							_selectedInstance->setAssetId("");
							_subsystem[RENDERING_SYSTEM]->onInstanceUpdated(*_selectedInstance);
						}
						for (const auto& assetId : _resourceManager->listAssetIds()) {
							bool isSourceSelected = (assetId == currentAssetId);
							if (ImGui::Selectable(assetId.c_str(), isSourceSelected)) {
								_selectedInstance->setAssetId(assetId);
								_subsystem[RENDERING_SYSTEM]->onInstanceUpdated(*_selectedInstance);
							}
						}
						ImGui::EndCombo();
					}

					if (currentField) {
						ImGui::BulletText("Name: %s", currentField->name.c_str());

						ImGui::Bullet();
						ImGui::SameLine();
						ImGui::Text("Path: ");
						ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
						ImGui::TextWrapped("%s", currentField->path.c_str());
						ImGui::PopTextWrapPos();

						ImGui::BulletText("Points: %u", currentField->count);
						ImGui::BulletText("CPU Source SH Degree: %d", currentField->sh_degree);
						const auto gpuField = GPUResourceManager::getInstance().getField(currentAssetId);
						if (gpuField) {
							ImGui::BulletText("GPU Uploaded SH Degree: %d", gpuField->sh_degree);
							ImGui::BulletText("GPU Asset Bytes: %s MB", formatMegabytes(gpuField->bytes).c_str());
							if (gpuField->sh_degree != _maxShDegree) {
								ImGui::TextDisabled("Resident GPU asset keeps its upload-time SH setting.");
							}
						}
						else {
							ImGui::BulletText("GPU Uploaded SH Degree: not resident");
						}
					}
				}

				ImGui::Spacing();
				if (ImGui::Button("Delete Instance", ImVec2(-1, 25))) {
					_subsystem[RENDERING_SYSTEM]->onInstanceRemoved(*_selectedInstance);
					_scene->removeInstance(_selectedInstance->getName());
					_selectedInstance = nullptr;
				}
			}
			else {
				ImGui::TextDisabled("Select an instance to edit its properties.");
			}
		}
		ImGui::End();
	}

	void ExtendedGaussianViewer::onShowResourceBrowser(Window& win) {
		float browserHeight = 220.0f;
		float sideWidth = 350.0f;
		ImGui::SetNextWindowPos(ImVec2(0, win.size().y() - browserHeight), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(win.size().x() - sideWidth, browserHeight), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Resource Browser", &_showResourceBrowser, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
			if (ImGui::Button("Load Manifest", ImVec2(120, 30))) {
				std::string manifestPath;
				if (showFilePicker(manifestPath, FilePickerMode::Default, "", "json")) {
					loadManifestFile(manifestPath);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Import PLY", ImVec2(100, 30))) {
				std::string path;
				if (showFilePicker(path, FilePickerMode::Directory)) {
					auto field = GaussianLoader::load(path);
					if (field) {
						const std::string importedAssetId = field->name;
						if (_resourceManager->addField(std::move(field))) {
							const auto assetIds = _resourceManager->listAssetIds();
							if (assetIds.size() == 1 && assetIds.front() == importedAssetId) {
								focusCameraOnAssetCenter(importedAssetId);
							}
						}
					}
				}
			}
			ImGui::SameLine();
			ImGui::Text("Max SH: SH %d", clampShDegree(_maxShDegree));
			ImGui::TextDisabled("Max SH is fixed by the low-VRAM default profile. Resident GPU assets are not reuploaded automatically.");
			if (!_resourceBrowserStatus.empty()) {
				ImGui::TextWrapped("%s", _resourceBrowserStatus.c_str());
			}
			ImGui::Separator();

			ImGui::TextWrapped("Manifest: %s", _loadedManifestPath.empty() ? "(none)" : _loadedManifestPath.c_str());
			ImGui::Text("CPU Resident: %s MB", formatMegabytes(_resourceManager->totalCpuBytes()).c_str());
			const RenderingSystem* renderingSystem = getRenderingSystem();
			ImGui::TextUnformatted("VRAM (partial accounting):");
			ImGui::Text("  GPU Assets:       %s MB", formatMegabytes(GPUResourceManager::getInstance().totalBytes()).c_str());
			if (renderingSystem) {
				if (const auto* view = renderingSystem->getView("Gaussian View")) {
					const Vector2i& internalResolution = view->getResolution();
					ImGui::Text("  Render scale:     %.2f (%dx%d)", view->renderScale(), internalResolution.x(), internalResolution.y());
					ImGui::Text("  Splat radius cap: %d px", view->maxSplatRadius());
					ImGui::Text("  World buffers:    %s MB", formatMegabytes(view->worldBufferBytes()).c_str());
					ImGui::Text("  Scratch (rast):   %s MB", formatMegabytes(view->scratchBufferBytes()).c_str());
					ImGui::Text("  Output+Interop:   %s MB", formatMegabytes(view->outputInteropBytes()).c_str());
				}
			}
			{
				size_t freeB = 0, totalB = 0;
				if (cudaMemGetInfo(&freeB, &totalB) == cudaSuccess) {
					ImGui::Text("  CUDA allocatable used (device): %s / %s MB",
						formatMegabytes(totalB - freeB).c_str(),
						formatMegabytes(totalB).c_str());
				}
			}
			ImGui::TextDisabled("* excludes CUDA runtime/context, cuBLAS/cuDNN, driver overhead");
			const SwapManager::Stats* stats = renderingSystem ? renderingSystem->getSwapStats() : nullptr;
			if (stats) {
				ImGui::Text("Phase: %s", stats->current_phase.empty() ? "(none)" : stats->current_phase.c_str());
				ImGui::Text("Required GPU: %u | Warm CPU: %u", static_cast<unsigned>(stats->required_gpu_count), static_cast<unsigned>(stats->warm_cpu_count));
				ImGui::Text("Disk Loads: %u | Uploads: %u | Evicts: %u",
					static_cast<unsigned>(stats->pending_disk_loads),
					static_cast<unsigned>(stats->pending_gpu_uploads),
					static_cast<unsigned>(stats->pending_gpu_evictions));
				ImGui::Text("Swap Hits: %u | Misses: %u | Skipped Instances: %u",
					static_cast<unsigned>(stats->swap_hits),
					static_cast<unsigned>(stats->swap_misses),
					static_cast<unsigned>(stats->skipped_instances_last_frame));
			}
			ImGui::Separator();

			float tileWidth = 120.0f;
			float tileHeight = 140.0f;
			float padding = 12.0f;
			float panelWidth = ImGui::GetContentRegionAvail().x;
			int columnCount = std::max(1, (int)(panelWidth / (tileWidth + padding)));

			const bool assetGridOpen = ImGui::BeginChild("AssetGrid");
			if (assetGridOpen) {
				const auto allAssets = _resourceManager->snapshotAssets();
				int n = 0;
				std::string fieldPendingDelete;

				for (const auto& asset : allAssets) {
					bool isSelected = (_selectedField == asset.id);
					const GpuState gpuState = GPUResourceManager::getInstance().state(asset.id);

					ImGui::PushID(asset.id.c_str());
					ImGui::BeginGroup();

					if (ImGui::Selectable("##tile", isSelected, 0, ImVec2(tileWidth, tileHeight))) {
						_selectedField = asset.id;
					}

					if (ImGui::BeginPopupContextItem("AssetCtx")) {
						if (_manifestStore.assets().find(asset.id) == _manifestStore.assets().end()) {
							if (ImGui::MenuItem("Delete Asset")) {
								fieldPendingDelete = asset.id;
							}
						}
						ImGui::EndPopup();
					}

					ImVec2 cursorPos = ImGui::GetItemRectMin();
					ImVec2 center = ImVec2(cursorPos.x + tileWidth * 0.5f, cursorPos.y + tileHeight * 0.4f);

					ImGui::GetWindowDrawList()->AddCircleFilled(center, 30.0f, ImColor(100, 150, 255, 200));
					ImGui::GetWindowDrawList()->AddCircleFilled(center, 15.0f, ImColor(200, 220, 255, 255));

					ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 5, cursorPos.y + tileHeight - 30));
					ImGui::PushTextWrapPos(cursorPos.x + tileWidth - 5);
					ImGui::TextUnformatted(asset.id.c_str());
					ImGui::TextDisabled("%s / %s", cpuStateLabel(asset.cpu_state), gpuStateLabel(gpuState));
					ImGui::PopTextWrapPos();

					ImGui::EndGroup();
					ImGui::PopID();

					if ((n + 1) % columnCount != 0) {
						ImGui::SameLine(0, padding);
					}
					n++;
				}

				if (!fieldPendingDelete.empty()) {
					const size_t referenceCount = _scene->countInstancesUsingAsset(fieldPendingDelete);
					if (referenceCount > 0) {
						SIBR_WRG << "Cannot delete asset '" << fieldPendingDelete << "' because " << referenceCount << " instance(s) still reference it." << std::endl;
					}
					else {
						GPUResourceManager::getInstance().removeField(fieldPendingDelete);
						_resourceManager->removeField(fieldPendingDelete);
						if (_selectedField == fieldPendingDelete) {
							_selectedField.clear();
						}
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}

	void ExtendedGaussianViewer::onShowCapturePanel(Window& win) {

		float sideWidth = 350.0f;
		ImGui::SetNextWindowPos(ImVec2(10.0f, 20.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(sideWidth, 400.0f), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("ClassRoom", &_showCapturePanel)) {
			CaptureSystem* captureSys = static_cast<CaptureSystem*>(_subsystem[CAPTURE_SYSTEM].get());

			bool openCreateModal = false;

			// 1. 캡처 리스트 (Child Window)
			const bool listOpen = ImGui::BeginChild("CaptureList", ImVec2(0, 0), true);
			if (listOpen) {
				// 빈 공간 우클릭 시 생성 메뉴
				if (ImGui::BeginPopupContextWindow("CaptureListContext", 1)) {
					if (ImGui::MenuItem("Create Capture")) {
						openCreateModal = true;
					}
					ImGui::EndPopup();
				}

				if (captureSys) {
					const auto& allCaptures = captureSys->getCaptures();
					std::string captureToDelete = ""; 

					for (const auto& pair : allCaptures) {
						sibr::Capture* cap = pair.second.get();

						// 고유 ID 푸시 (아이템별 독립적인 우클릭 메뉴를 위해 필요)
						ImGui::PushID(cap->name.c_str());

						bool clicked = ImGui::Selectable(cap->name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);

						// 아이템 위에서 우클릭 시 삭제 메뉴 팝업
						if (ImGui::BeginPopupContextItem("CaptureItemMenu", 1)) {
							if (ImGui::MenuItem("Delete Capture")) {
								captureToDelete = cap->name;
							}
							ImGui::EndPopup();
						}

						if (clicked) {
							if (ImGui::IsMouseDoubleClicked(0)) {
								if (_selectedInstance) {
									// 1. Transform 업데이트
									_selectedInstance->getPositionRef() = cap->capture_position;
									_selectedInstance->getEulerRef() = cap->captured_euler_angle;
									_selectedInstance->getScaleRef() = cap->captured_scale;

									// 2. Field 업데이트
									if (cap->captured_field) {
										_selectedInstance->setAssetId(cap->captured_field->name);
									}

									_subsystem[RENDERING_SYSTEM]->onInstanceUpdated(*_selectedInstance);

									RenderingSystem* renderingSystem = getRenderingSystem();
									if (renderingSystem) {
										const auto viewIt = _ibrSubViews.find("Gaussian View");
										if (viewIt != _ibrSubViews.end()) {
											sibr::Matrix3f Rv = cap->captured_view.block<3, 3>(0, 0);
											sibr::Vector3f tv = cap->captured_view.block<3, 1>(0, 3);

											sibr::Vector3f cameraTranslation = -Rv.transpose() * tv;
											sibr::Quaternionf cameraRotation(Rv.transpose());
											cameraRotation.normalize();

											// 1. 카메라 데이터 구조체 업데이트
											viewIt->second.cam.set(cameraTranslation, cameraRotation);

											// 2. CustomCameraHandler로 동기화
											auto customHandler = std::dynamic_pointer_cast<CustomCameraHandler>(viewIt->second.handler);

											if (customHandler) {
												customHandler->fromCamera(static_cast<sibr::InputCamera&>(viewIt->second.cam));
											}
											else {
												SIBR_WRG << "Handler is not a CustomCameraHandler! Cannot sync." << std::endl;
											}
										}
									}

									SIBR_LOG << "Capture [" << cap->name << "] applied to Instance [" << _selectedInstance->getNameRef() << "]" << std::endl;
								}
								else {
									SIBR_WRG << "No instance selected in Scene Outliner to apply capture!" << std::endl;
								}
							}
						}
						ImGui::PopID(); 
					}

					if (!captureToDelete.empty()) {
						captureSys->removeCapture(captureToDelete);
						SIBR_LOG << "Capture Deleted: " << captureToDelete << std::endl;
					}
				}
			}
			ImGui::EndChild();

			// 2. 캡처 생성 모달 트리거 및 렌더링

			if (openCreateModal) {
				ImGui::OpenPopup("Create New Capture");
			}

			if (ImGui::BeginPopupModal("Create New Capture", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				static char capNameBuf[128] = "NewCapture";
				static std::string targetInstanceName = "";
				static std::string targetAssetId = "";

				ImGui::InputText("Name", capNameBuf, IM_ARRAYSIZE(capNameBuf));
				ImGui::Spacing();

				if (ImGui::BeginCombo("Instance", targetInstanceName.empty() ? "Select..." : targetInstanceName.c_str())) {
					for (const auto& pair : _scene->getInstances()) {
						if (ImGui::Selectable(pair.first.c_str(), targetInstanceName == pair.first)) {
							targetInstanceName = pair.first;
						}
					}
					ImGui::EndCombo();
				}

				if (ImGui::BeginCombo("Field", targetAssetId.empty() ? "Select..." : targetAssetId.c_str())) {
					for (const auto& assetId : _resourceManager->listAssetIds()) {
						if (ImGui::Selectable(assetId.c_str(), targetAssetId == assetId)) {
							targetAssetId = assetId;
						}
					}
					ImGui::EndCombo();
				}

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Save", ImVec2(120, 0))) {
					GaussianInstance* instToCapture = nullptr;
					auto it = _scene->getInstances().find(targetInstanceName);
					if (it != _scene->getInstances().end()) {
						instToCapture = it->second.get();
					}

					const GaussianField* fieldToCapture = _resourceManager->getCpuFieldShared(targetAssetId).get();

					if (instToCapture && fieldToCapture) {
						RenderingSystem* renderingSystem = getRenderingSystem();
						if (renderingSystem) {
							const auto viewIt = _ibrSubViews.find("Gaussian View");
							if (viewIt != _ibrSubViews.end()) {
								Matrix4f currentViewMatrix = viewIt->second.cam.view();

								captureSys->addCapture(
									capNameBuf,
									instToCapture,
									fieldToCapture,
									instToCapture->getPositionRef(),
									instToCapture->getEulerRef(),
									instToCapture->getScaleRef(),
									currentViewMatrix
								);

								SIBR_LOG << "Capture Created: " << capNameBuf << std::endl;
							}
							else {
								SIBR_WRG << "Failed to find 'Gaussian View'. Capture not created." << std::endl;
							}
						}

						ImGui::CloseCurrentPopup();
						strcpy(capNameBuf, "NewCapture");
						targetInstanceName.clear();
						targetAssetId.clear();
					}
					else {
						SIBR_WRG << "Please select both a valid instance and field." << std::endl;
					}
				}

				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					strcpy(capNameBuf, "NewCapture");
					targetInstanceName.clear();
					targetAssetId.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}

	void ExtendedGaussianViewer::toggleGUI()
	{
		_showGUI = !_showGUI;
		if (!_showGUI) {
			SIBR_LOG << "[MultiViewManager] GUI is now hidden, use Ctrl+Alt+G to toggle it back on." << std::endl;
		}
		if (_uiMode == UIMode::User) {
			return;
		}
		toggleSubViewsGUI();
	}
}
