#include <cstdlib>

#include <core/graphics/Window.hpp>
#include <core/system/CommandLineArgs.hpp>
#include <core/system/String.hpp>
#include "projects/extended_gaussian/renderer/ExtendedGaussianViewer.hpp"

using namespace sibr;

int main(int ac, char** av)
{
#ifdef _WIN32
	_putenv_s("CUDA_MODULE_LOADING", "LAZY");
	_putenv_s("SIBR_IMGUI_KOREAN_FONT", "C:\\Windows\\Fonts\\malgun.ttf");
#else
	setenv("CUDA_MODULE_LOADING", "LAZY", 1);
#endif

	CommandLineArgs::parseMainArgs(ac, av);
	BasicIBRAppArgs myArgs;

	sibr::Window window("Extended Gaussian User Viewer", sibr::Vector2i(50, 50), myArgs);
	ExtendedGaussianViewer viewer(window, false, ExtendedGaussianViewer::UIMode::User);

	while (window.isOpened())
	{
		sibr::Input::poll();
		window.makeContextCurrent();

		if (sibr::Input::global().key().isPressed(sibr::Key::Escape))
		{
			window.close();
		}

		viewer.onUpdate(sibr::Input::global());
		viewer.onRender(window);

		viewer.onSwapBuffer(window);
	}

	return EXIT_SUCCESS;
}
