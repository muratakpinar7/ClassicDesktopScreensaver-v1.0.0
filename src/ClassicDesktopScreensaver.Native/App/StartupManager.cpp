#include "StartupManager.h"

#include <Windows.h>
#include <ShlObj.h>
#include <Shobjidl.h>

#include <filesystem>
#include <memory>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr wchar_t ShortcutName[] = L"Classic Desktop Screensaver.lnk";

        struct CoTaskMemDeleter
        {
            void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
        };

        class ComPtr final
        {
        public:
            explicit ComPtr(IUnknown* value = nullptr) noexcept : value_(value) {}
            ~ComPtr() { if (value_ != nullptr) value_->Release(); }
            ComPtr(const ComPtr&) = delete;
            ComPtr& operator=(const ComPtr&) = delete;
            template<typename T> T* Get() const noexcept { return static_cast<T*>(value_); }
        private:
            IUnknown* value_{};
        };

        std::wstring CurrentExecutable()
        {
            std::wstring result(32768, L'\0');
            const DWORD size = GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
            if (size == 0 || size >= result.size())
            {
                return {};
            }
            result.resize(size);
            return result;
        }
    }

    bool StartupManager::Apply(bool enabled, std::wstring& error) const
    {
        error.clear();
        PWSTR rawStartup = nullptr;
        const HRESULT knownFolder = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &rawStartup);
        if (FAILED(knownFolder) || rawStartup == nullptr)
        {
            error = L"The current user's Startup folder could not be found.";
            return false;
        }
        const std::unique_ptr<wchar_t, CoTaskMemDeleter> startupFolder(rawStartup);
        std::filesystem::path shortcutPath(startupFolder.get());
        shortcutPath /= ShortcutName;

        if (!enabled)
        {
            if (DeleteFileW(shortcutPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                return true;
            }
            error = L"The Startup shortcut could not be removed.";
            return false;
        }

        const std::wstring executable = CurrentExecutable();
        if (executable.empty())
        {
            error = L"The application path could not be determined.";
            return false;
        }

        IShellLinkW* rawLink = nullptr;
        HRESULT result = CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&rawLink));
        if (FAILED(result) || rawLink == nullptr)
        {
            error = L"Windows could not create a Startup shortcut.";
            return false;
        }
        ComPtr link(reinterpret_cast<IUnknown*>(rawLink));

        IShellLinkW* shellLink = link.Get<IShellLinkW>();
        result = shellLink->SetPath(executable.c_str());
        if (SUCCEEDED(result)) result = shellLink->SetArguments(L"--background");
        if (SUCCEEDED(result)) result = shellLink->SetWorkingDirectory(executable.substr(0, executable.find_last_of(L"\\/")).c_str());
        if (SUCCEEDED(result)) result = shellLink->SetDescription(L"Classic Desktop Screensaver");
        if (FAILED(result))
        {
            error = L"Windows could not configure the Startup shortcut.";
            return false;
        }

        IPersistFile* rawPersist = nullptr;
        result = shellLink->QueryInterface(IID_PPV_ARGS(&rawPersist));
        if (FAILED(result) || rawPersist == nullptr)
        {
            error = L"Windows could not save the Startup shortcut.";
            return false;
        }
        ComPtr persist(reinterpret_cast<IUnknown*>(rawPersist));
        result = persist.Get<IPersistFile>()->Save(shortcutPath.c_str(), TRUE);
        if (FAILED(result))
        {
            error = L"The Startup shortcut could not be saved.";
            return false;
        }
        return true;
    }
}
