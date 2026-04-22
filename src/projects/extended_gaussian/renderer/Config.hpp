#pragma once

#include <core/system/Config.hpp>
#include <core/system/CommandLineArgs.hpp>

#ifdef SIBR_OS_WINDOWS
#ifdef SIBR_STATIC_DEFINE
#define SIBR_EXPORT
#define SIBR_NO_EXPORT
#else
#ifndef SIBR_EXTENDED_GAUSSIAN_EXPORT
#ifdef SIBR_EXTENDED_GAUSSIAN_EXPORTS
#define SIBR_EXTENDED_GAUSSIAN_EXPORT __declspec(dllexport)
#else
#define SIBR_EXTENDED_GAUSSIAN_EXPORT __declspec(dllimport)
#endif
#endif
#ifndef SIBR_NO_EXPORT
#define SIBR_NO_EXPORT
#endif
#endif
#else
#define SIBR_EXTENDED_GAUSSIAN_EXPORT
#endif