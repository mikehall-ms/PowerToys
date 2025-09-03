#pragma once

#define _MSVC_COROUTINE_ABI 1
#define COMPOSITION
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <windows.ui.composition.interop.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <thread>
#include <atomic>
#include <string>
#include <common/interop/shared_constants.h>
#include "../MousePointerCrosshairs/InclusiveCrosshairs.h"
