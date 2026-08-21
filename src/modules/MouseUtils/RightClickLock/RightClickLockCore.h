// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#pragma once

#include <windows.h>
#include <atomic>

// Core state machine for Right Click Lock.
//
// Implements ClickLock semantics for the right mouse button: after the user taps and
// holds RMB past a configurable threshold, releasing the button leaves the OS believing
// RMB is still held. The next RMB press releases the synthetic hold.
//
// All Handle* methods are expected to be called on the thread that owns the WH_MOUSE_LL
// hook. EmergencyRelease() is safe to call from any thread and is used by the crash-safe
// release paths (module disable/destroy, session lock/logoff, panic hotkey).
class RightClickLockCore
{
public:
	// Magic tag stored in MOUSEINPUT::dwExtraInfo so the hook can recognize and ignore
	// events this module injects, avoiding feedback loops.
	static constexpr ULONG_PTR InjectionTag = 0x52434C4Bull; // 'RCLK'

	// Timer id used to arm the lock while the button is held.
	static constexpr UINT_PTR ArmTimerId = 0xB1C2;

	void SetHoldDelayMs(int ms) noexcept { m_holdDelayMs = ms; }
	void SetMoveCancelPixels(int px) noexcept { m_moveCancelPixels = px; }

	// Hook event handlers. Return true when the event must be suppressed (drop it so the
	// OS/apps never see it).
	bool HandleRightButtonDown(const MSLLHOOKSTRUCT& info, HWND timerHost);
	bool HandleRightButtonUp(const MSLLHOOKSTRUCT& info);
	bool HandleMouseMove(const MSLLHOOKSTRUCT& info);

	// Called when the arming timer fires on the hook thread.
	void OnArmTimer();

	// Force-release the synthetic hold if one is active. Safe to call from any thread.
	void EmergencyRelease();

	// Clear transient state without releasing. Call EmergencyRelease() first if a lock
	// might be active.
	void Reset() noexcept;

	bool IsLocked() const noexcept { return m_held.load(std::memory_order_acquire); }

private:
	enum class State
	{
		Idle,
		Holding,
		Armed,
		Locked,
		Releasing,
	};

	void InjectRightUp();
	void InjectRightDown();

	State m_state = State::Idle;
	POINT m_downPos{};
	bool m_moveCancelled = false;
	HWND m_timerHost = nullptr;
	int m_holdDelayMs = 300;
	int m_moveCancelPixels = 10;

	// Single-bit signal shared across the hook thread, the UI/listener thread and the
	// emergency-release callers. Guards against double release.
	std::atomic<bool> m_held{ false };
};
