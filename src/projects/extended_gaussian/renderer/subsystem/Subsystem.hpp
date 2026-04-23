#pragma once

# include <core/system/Config.hpp>
#include "Config.hpp"

namespace sibr {
	enum SubsystemType {
		RENDERING_SYSTEM = 0,
		ARCHIVE_SYSTEM = 1,
		UI_SYSTEM = 2,
		SUBSYSTEM_LAST
	};

	class ExtendedGaussianViewer;
	class GaussianInstance;

	class SIBR_EXTENDED_GAUSSIAN_EXPORT Subsystem {
	public:
		SIBR_CLASS_PTR(Subsystem);

		virtual void onSystemAdded(ExtendedGaussianViewer& owner) = 0;

		virtual void onSystemRemoved(ExtendedGaussianViewer& owner) = 0;

		virtual void onInstanceCreated(GaussianInstance& instance) {}

		virtual void onInstanceUpdated(GaussianInstance& instance) {}

		virtual void onInstanceRemoved(GaussianInstance& instance) {}

		virtual void onRender() {}

	protected:
		SubsystemType type;
	};
}
