#include "NotificationService.h"

#include "Localization.h"

#include <Windows.h>
#include <ShlObj.h>
#include <Shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include <filesystem>
#include <memory>
#include <string>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "WindowsApp.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        constexpr wchar_t AppUserModelId[] = L"OpenAI.ClassicDesktopScreensaver.Native";
        constexpr wchar_t ShortcutName[] = L"Classic Desktop Screensaver Native.lnk";

        struct CoTaskMemDeleter
        {
            void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
        };

        template<typename T>
        class ComPtr final
        {
        public:
            ComPtr() noexcept = default;
            ~ComPtr() { if (value_ != nullptr) value_->Release(); }
            ComPtr(const ComPtr&) = delete;
            ComPtr& operator=(const ComPtr&) = delete;
            T** Put() noexcept
            {
                if (value_ != nullptr) { value_->Release(); value_ = nullptr; }
                return &value_;
            }
            T* Get() const noexcept { return value_; }
            T* operator->() const noexcept { return value_; }
        private:
            T* value_{};
        };

        bool EnsureShortcut()
        {
            PWSTR rawPrograms = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &rawPrograms)) ||
                rawPrograms == nullptr)
            {
                return false;
            }
            const std::unique_ptr<wchar_t, CoTaskMemDeleter> programsFolder(rawPrograms);
            std::filesystem::path shortcutPath(programsFolder.get());
            shortcutPath /= ShortcutName;

            std::wstring executable(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (length == 0 || length >= executable.size()) return false;
            executable.resize(length);

            ComPtr<IShellLinkW> link;
            HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(link.Put()));
            if (FAILED(result)) return false;
            result = link->SetPath(executable.c_str());
            if (SUCCEEDED(result)) result = link->SetDescription(L"Classic Desktop Screensaver notification identity");
            if (FAILED(result)) return false;

            ComPtr<IPropertyStore> properties;
            result = link->QueryInterface(IID_PPV_ARGS(properties.Put()));
            if (FAILED(result)) return false;
            PROPVARIANT appId{};
            result = InitPropVariantFromString(AppUserModelId, &appId);
            if (SUCCEEDED(result)) result = properties->SetValue(PKEY_AppUserModel_ID, appId);
            if (SUCCEEDED(result)) result = properties->Commit();
            PropVariantClear(&appId);
            if (FAILED(result)) return false;

            ComPtr<IPersistFile> persist;
            result = link->QueryInterface(IID_PPV_ARGS(persist.Put()));
            if (FAILED(result)) return false;
            return SUCCEEDED(persist->Save(shortcutPath.c_str(), TRUE));
        }
    }

    void ShowStatusToast(bool enabled, AppLanguage language) noexcept
    {
        try
        {
            if (FAILED(SetCurrentProcessExplicitAppUserModelID(AppUserModelId)) || !EnsureShortcut())
            {
                return;
            }

            using namespace winrt::Windows::Data::Xml::Dom;
            using namespace winrt::Windows::UI::Notifications;
            XmlDocument document;
            document.LoadXml(L"<toast><visual><binding template=\"ToastGeneric\"><text></text><text></text></binding></visual></toast>");
            const auto textNodes = document.GetElementsByTagName(L"text");
            textNodes.Item(0).AppendChild(document.CreateTextNode(
                winrt::hstring(Text(language, L"Title"))));
            const std::wstring status(Text(language, enabled ? L"StatusOn" : L"StatusOff"));
            textNodes.Item(1).AppendChild(document.CreateTextNode(winrt::hstring(status)));
            ToastNotification toast(document);
            ToastNotificationManager::CreateToastNotifier().Show(toast);
        }
        catch (...)
        {
            // Portable deployments and Windows notification policies can reject
            // desktop toasts. Notifications never affect application behavior.
        }
    }
}
