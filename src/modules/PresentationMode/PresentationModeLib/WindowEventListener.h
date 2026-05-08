#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace PresentationMode
{
    // Listens for window-show / window-uncloak events on a dedicated message-pump thread
    // and forwards each new HWND to the provided callback.
    class WindowEventListener
    {
    public:
        using Callback = std::function<void(HWND)>;

        WindowEventListener();
        ~WindowEventListener();

        WindowEventListener(const WindowEventListener&) = delete;
        WindowEventListener& operator=(const WindowEventListener&) = delete;

        // Starts the hook thread. Safe to call multiple times; subsequent calls are no-ops.
        void Start(Callback cb);

        // Stops and joins the hook thread.
        void Stop();

    private:
        static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD eventThread, DWORD eventTime);
        void HandleEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild) noexcept;
        void ThreadProc();

        std::mutex m_mutex;
        Callback m_callback;
        std::thread m_thread;
        std::atomic<DWORD> m_threadId{ 0 };
        std::atomic<bool> m_running{ false };
        std::vector<HWINEVENTHOOK> m_hooks;

        // The hook procedure runs on the thread that installed the hook,
        // so a single instance pointer is sufficient.
        static inline std::atomic<WindowEventListener*> s_instance{ nullptr };
    };
}
