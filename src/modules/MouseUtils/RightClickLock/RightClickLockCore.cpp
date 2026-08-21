// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "pch.h"
#include "RightClickLockCore.h"
#include "../../../common/logger/logger.h"

// Debug-only tracing so the lock engaging/releasing can be observed live (module log
// file plus the debugger Output window). Compiled out entirely in Release.
#ifdef _DEBUG
#define RCL_DBG(msg)                                     \
	do                                                   \
	{                                                    \
		Logger::debug("[RightClickLock] " msg);          \
		OutputDebugStringW(L"[RightClickLock] " L##msg L"\n"); \
	} while (0)
#else
#define RCL_DBG(msg) ((void)0)
#endif

bool RightClickLockCore::HandleRightButtonDown(const MSLLHOOKSTRUCT& info, HWND timerHost)
{
	// Ignore the events we injected ourselves.
	if (info.dwExtraInfo == InjectionTag)
	{
		return false;
	}

	m_timerHost = timerHost;

	if (m_state == State::Locked)
	{
		// A synthetic hold is active and the user pressed RMB again to release it.
		// Suppress this physical DOWN, release the hold, and swallow the physical UP
		// that will follow.
		m_state = State::Releasing;
		RCL_DBG("Locked -> Releasing (user pressed RMB to release the hold)");
		InjectRightUp();
		return true;
	}

	// Start a fresh hold cycle and begin arming.
	m_state = State::Holding;
	m_downPos = info.pt;
	m_moveCancelled = false;
	if (m_timerHost)
	{
		SetTimer(m_timerHost, ArmTimerId, static_cast<UINT>(m_holdDelayMs), nullptr);
	}
	RCL_DBG("Idle -> Holding (RMB down, arming timer started)");

	// Let the application see the DOWN.
	return false;
}

bool RightClickLockCore::HandleRightButtonUp(const MSLLHOOKSTRUCT& info)
{
	// The synthetic UP passes through so the OS actually releases the button.
	if (info.dwExtraInfo == InjectionTag)
	{
		return false;
	}

	switch (m_state)
	{
	case State::Releasing:
		// Swallow the physical UP that follows a release click.
		m_state = State::Idle;
		RCL_DBG("Releasing -> Idle (physical UP after release swallowed)");
		return true;

	case State::Armed:
		// Lock: the physical release would drop GetAsyncKeyState/raw-input to "up", so we
		// inject a synthetic DOWN to keep the button held at the system level, and suppress
		// the physical UP so message-stream consumers keep seeing a continuous hold.
		if (m_timerHost)
		{
			KillTimer(m_timerHost, ArmTimerId);
		}
		m_state = State::Locked;
		InjectRightDown();
		m_held.store(true, std::memory_order_release);
		RCL_DBG("Armed -> Locked (synthetic RIGHTDOWN injected, physical UP suppressed)");
		return true;

	default:
		// Holding (not yet armed) or move-cancelled: a normal right-click.
		if (m_timerHost)
		{
			KillTimer(m_timerHost, ArmTimerId);
		}
		m_state = State::Idle;
		m_moveCancelled = false;
		RCL_DBG("Holding -> Idle (released before arming: normal right-click)");
		return false;
	}
}

bool RightClickLockCore::HandleMouseMove(const MSLLHOOKSTRUCT& info)
{
	if (info.dwExtraInfo == InjectionTag)
	{
		return false;
	}

	if (m_state == State::Holding && !m_moveCancelled)
	{
		const long long dx = static_cast<long long>(info.pt.x) - m_downPos.x;
		const long long dy = static_cast<long long>(info.pt.y) - m_downPos.y;
		const long long distSq = dx * dx + dy * dy;
		const long long threshSq = static_cast<long long>(m_moveCancelPixels) * m_moveCancelPixels;

		// Comparing squared distances avoids a sqrt on every mouse-move event.
		if (distSq > threshSq)
		{
			m_moveCancelled = true;
			if (m_timerHost)
			{
				KillTimer(m_timerHost, ArmTimerId);
			}
		}
	}

	return false;
}

void RightClickLockCore::OnArmTimer()
{
	if (m_timerHost)
	{
		KillTimer(m_timerHost, ArmTimerId);
	}

	if (m_state == State::Holding && !m_moveCancelled)
	{
		// Only arm if the button is still physically held.
		if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
		{
			m_state = State::Armed;
			RCL_DBG("Holding -> Armed (hold delay elapsed, RMB still down)");
		}
		else
		{
			m_state = State::Idle;
			RCL_DBG("Holding -> Idle (arm timer fired but RMB no longer down)");
		}
	}
}

void RightClickLockCore::EmergencyRelease()
{
	// exchange() guarantees only one caller actually injects the release.
	if (m_held.exchange(false, std::memory_order_acq_rel))
	{
		INPUT input = {};
		input.type = INPUT_MOUSE;
		input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
		input.mi.dwExtraInfo = InjectionTag;
		SendInput(1, &input, sizeof(INPUT));
		RCL_DBG("EmergencyRelease (synthetic RIGHTUP injected)");
	}

	m_state = State::Idle;
	m_moveCancelled = false;
}

void RightClickLockCore::Reset() noexcept
{
	if (m_timerHost)
	{
		KillTimer(m_timerHost, ArmTimerId);
	}
	m_state = State::Idle;
	m_moveCancelled = false;
}

void RightClickLockCore::InjectRightUp()
{
	INPUT input = {};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
	input.mi.dwExtraInfo = InjectionTag;
	SendInput(1, &input, sizeof(INPUT));
	m_held.store(false, std::memory_order_release);
}

void RightClickLockCore::InjectRightDown()
{
	// Assert the button as held at the system level (GetAsyncKeyState / raw input), which a
	// mere message-suppression cannot do. The event is tagged so our own hook ignores it.
	INPUT input = {};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
	input.mi.dwExtraInfo = InjectionTag;
	SendInput(1, &input, sizeof(INPUT));
}
