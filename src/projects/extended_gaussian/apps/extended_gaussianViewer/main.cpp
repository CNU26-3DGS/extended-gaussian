#include <fstream>

#include <core/graphics/Window.hpp>
#include <core/view/MultiViewManager.hpp>
#include <core/system/String.hpp>
#include "projects/extended_gaussian/renderer/ExtendedGaussianViewer.hpp"

#include <core/renderer/DepthRenderer.hpp>
#include <core/raycaster/Raycaster.hpp>
#include <core/view/SceneDebugView.hpp>
#include <algorithm>
#include <cstdlib>
#include <boost/filesystem.hpp>
#include <regex>
#include <imgui/imgui_internal.h>

using namespace sibr;

int main(int ac, char** av)
{
#ifdef _WIN32
	_putenv_s("CUDA_MODULE_LOADING", "LAZY");
#else
	setenv("CUDA_MODULE_LOADING", "LAZY", 1);
#endif

	CommandLineArgs::parseMainArgs(ac, av);
	BasicIBRAppArgs myArgs;

	sibr::Window window("Extended Gaussian Viewer", sibr::Vector2i(50, 50), myArgs);

	ExtendedGaussianViewer viewer(window, false);

	while (window.isOpened())
	{
		sibr::Input::poll();
		window.makeContextCurrent();

		viewer.onUpdate(sibr::Input::global());
		viewer.onRender(window);

		viewer.onSwapBuffer(window);
	}

	return EXIT_SUCCESS;
}
