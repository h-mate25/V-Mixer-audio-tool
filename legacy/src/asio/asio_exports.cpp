#include "asio_driver.h"

#include <Objbase.h>
#include <Shlwapi.h>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

using windows_reac_asio::CLSID_WindowsReacASIO;
using windows_reac_asio::WindowsReacAsioDriver;

namespace {

constexpr wchar_t kDriverName[] = L"Windows REAC ASIO";
constexpr wchar_t kDriverDescription[] = L"Windows REAC ASIO Virtual Input Driver";
constexpr wchar_t kThreadingModel[] = L"Both";

std::wstring clsidToString(REFCLSID clsid) {
    wchar_t clsidText[64] = {};
    StringFromGUID2(clsid, clsidText, 64);
    return clsidText;
}

HRESULT setStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
}

HRESULT createKey(HKEY root, const std::wstring& path, HKEY* key) {
    const LONG rc = RegCreateKeyExW(
        root,
        path.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE | KEY_WOW64_64KEY,
        nullptr,
        key,
        nullptr);
    return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
}

class AsioClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }

        WindowsReacAsioDriver* driver = new (std::nothrow) WindowsReacAsioDriver();
        if (!driver) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = driver->QueryInterface(riid, object);
        driver->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) {
            windows_reac_asio::gServerLocks.fetch_add(1, std::memory_order_relaxed);
        } else {
            windows_reac_asio::gServerLocks.fetch_sub(1, std::memory_order_relaxed);
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{1};
};

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        windows_reac_asio::gModuleHandle = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void* GetAudioDriver() {
    static WindowsReacAsioDriver* driver = new WindowsReacAsioDriver();
    return static_cast<IASIO*>(driver);
}

extern "C" STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object) {
    if (!IsEqualCLSID(clsid, CLSID_WindowsReacASIO)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    AsioClassFactory* factory = new (std::nothrow) AsioClassFactory();
    if (!factory) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

extern "C" STDAPI DllCanUnloadNow() {
    return windows_reac_asio::gObjectCount.load(std::memory_order_relaxed) == 0 &&
                   windows_reac_asio::gServerLocks.load(std::memory_order_relaxed) == 0
               ? S_OK
               : S_FALSE;
}

extern "C" STDAPI DllRegisterServer() {
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(windows_reac_asio::gModuleHandle, modulePath, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const std::wstring clsidText = clsidToString(CLSID_WindowsReacASIO);
    HKEY key = nullptr;

    HRESULT hr = createKey(HKEY_CLASSES_ROOT, L"CLSID\\" + clsidText, &key);
    if (FAILED(hr)) {
        return hr;
    }
    hr = setStringValue(key, nullptr, kDriverDescription);
    RegCloseKey(key);
    if (FAILED(hr)) {
        return hr;
    }

    hr = createKey(HKEY_CLASSES_ROOT, L"CLSID\\" + clsidText + L"\\InprocServer32", &key);
    if (FAILED(hr)) {
        return hr;
    }
    hr = setStringValue(key, nullptr, modulePath);
    if (SUCCEEDED(hr)) {
        hr = setStringValue(key, L"ThreadingModel", kThreadingModel);
    }
    RegCloseKey(key);
    if (FAILED(hr)) {
        return hr;
    }

    hr = createKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\" + std::wstring(kDriverName), &key);
    if (FAILED(hr)) {
        return hr;
    }
    hr = setStringValue(key, L"CLSID", clsidText);
    if (SUCCEEDED(hr)) {
        hr = setStringValue(key, L"Description", kDriverDescription);
    }
    RegCloseKey(key);
    return hr;
}

extern "C" STDAPI DllUnregisterServer() {
    const std::wstring clsidText = clsidToString(CLSID_WindowsReacASIO);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (L"CLSID\\" + clsidText).c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (L"SOFTWARE\\ASIO\\" + std::wstring(kDriverName)).c_str());
    return S_OK;
}
