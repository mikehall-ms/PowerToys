#include "pch.h"
#include "WindowEventListener.h"

#include <array>

#include <common/logger/logger.h>

namespace PresentationMode
{
    WindowEventListener::WindowEventListener() = default;

    WindowEventListener::~WindowEventListener()
    {
        Stop();
    }

    void WindowEventListener::Start(Callback cb)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running.load())
        {
            m_callback = std::move(cb);
            return;
        }

        m_callback = std::move(cb);
        m_running.store(true);
        s_instance.store(this);

        m_thread = std::thread([this] { ThreadProc(); });

        // Wait briefly for the thread to register a thread id; this lets callers
        // post messages immediately if needed.
        for (int i = 0; i < 100 && m_threadId.load() == 0; ++i)
        {
            Sleep(1);
        }
    }

    void WindowEventListener::Stop()
    {
        std::thread thread_to_join;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running.load())
            {
                return;
            }
            m_running.store(false);

            const DWORD tid = m_threadId.load();
            if (tid != 0)
            {
                PostThreadMessageW(tid, WM_QUIT, 0, 0);
            }

            thread_to_join = std::move(m_thread);
        }

        if (thread_to_join.joinable())
        {
            thread_to_join.join();
        }

        s_instance.store(nullptr);
        m_threadId.store(0);
        m_callback = nullptr;
    }

    void WindowEventListener::ThreadProc()
    {
        m_threadId.store(GetCurrentThreadId());

        // Subscribe to events that fire when a top-level window first becomes visible.
        constexpr std::array<DWORD, 2> events_to_subscribe = {
            EVENT_OBJECT_SHOW,
            EVENT_OBJECT_UNCLOAKED,
        };

        for (const DWORD event : events_to_subscribe)
        {
            HWINEVENTHOOK hook = SetWinEventHook(event, event, nullptr, &WindowEventListener::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (hook)
            {
                m_hooks.push_back(hook);
            }
            else
            {
                Logger::error(L"PresentationMode: Failed to install win event hook for event {}", event);
            }
        }

        // Standard message pump - required for WINEVENT_OUTOFCONTEXT callbacks.
        MSG msg{};
        while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        for (HWINEVENTHOOK hook : m_hooks)
        {
            UnhookWinEvent(hook);
        }
        m_hooks.clear();
    }

    void CALLBACK WindowEventListener::WinEventProc(HWINEVENTHOOK /*hook*/, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD /*eventThread*/, DWORD /*eventTime*/)
    {
        WindowEventListener* instance = s_instance.load();
        if (instance && instance->m_running.load())
        {
            instance->HandleEvent(event, hwnd, idObject, idChild);
        }
    }

    void WindowEventListener::HandleEvent(DWORD /*event*/, HWND hwnd, LONG idObject, LONG idChild) noexcept
    {
        // Only the window object itself is interesting; ignore child / non-window accessibility events.
        if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        {
            return;
        }

        if (!hwnd)
        {
            return;
        }

        Callback cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cb = m_callback;
        }

        if (cb)
        {
            try
            {
                cb(hwnd);
            }
            catch (...)
            {
                Logger::error(L"PresentationMode: window event callback threw an exception");
            }
        }
    }
}
