// dear imgui: Platform Backend for Universal Windows Platform (standard windows API for 32-bits AND 64-bits applications)
// This needs to be used along with a Renderer (e.g. DirectX11, DirectX12, SDL2..)

// Implemented features:
//  [X] Platform: Clipboard support (for Win32 & UWP this is actually part of core dear imgui)
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen/Pen.
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass ImGuiKey values to all key functions e.g. ImGui::IsKeyPressed(ImGuiKey_Space).
//  [X] Platform: Gamepad support. Enabled with 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad'.
//  [X] Platform: Mouse cursor shape and visibility. Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "imgui.h"
#include "imgui_impl_uwp.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sdkddkver.h>
#include <tchar.h>
#include <wrl.h>

//WinRT
#include <Windows.UI.Core.h>
#include <CoreWindow.h>
#include <Windows.UI.Xaml.Controls.h>
#include <Windows.UI.Xaml.Hosting.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

// Disabling XInput on older SDKs that didn't allow LoadLibrary for the Application Family
#if  (NTDDI_VERSION < NTDDI_WIN10_CO)
#define IMGUI_IMPL_UWP_DISABLE_GAMEPAD
#endif

// Using XInput for gamepad (will load DLL dynamically)
#ifndef IMGUI_IMPL_UWP_DISABLE_GAMEPAD
#include <xinput.h>
typedef DWORD (WINAPI *PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE*);
#endif

// From CoreWindow.h

typedef HRESULT(_cdecl* PFN_CreateControlInput)(_In_ REFIID riid, _COM_Outptr_ void** ppv);
typedef HRESULT(_cdecl* PFN_CreateControlInputEx)(_In_ IUnknown* pCoreWindow, _In_ REFIID riid, _COM_Outptr_ void** ppv);

MIDL_INTERFACE("40BFE3E3-B75A-4479-AC96-475365749BB8")
ISystemCoreInputInterop : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetInputSource(__RPC__in_opt ::IUnknown * value) PURE;
    virtual HRESULT STDMETHODCALLTYPE put_MessageHandled(boolean value) PURE;
};

// Clipboard support
#if !defined(IMGUI_DISABLE_UWP_DEFAULT_CLIPBOARD_FUNCTIONS)
#include "imgui_internal.h"
#include <windows.applicationmodel.datatransfer.h>

static const char* ImGui_ImplUwp_GetClipboardTextFn(void* user_data_ctx);
static void        ImGui_ImplUwp_SetClipboardTextFn(void* user_data_ctx, const char* text);
#endif

struct ImGui_ImplUwp_Data
{
    ABI::Windows::UI::Core::ICoreWindow*               CoreWindow;
    ABI::Windows::UI::Xaml::Controls::ISwapChainPanel* SwapChainPanel;
    ABI::Windows::UI::Xaml::IFrameworkElement*         SwapChainPanelElement;
    ABI::Windows::UI::Core::ICorePointerInputSource*   PointerInputSource;
    ABI::Windows::UI::Core::ICoreKeyboardInputSource*  KeyboardInputSource;
    int                                                MouseTrackedArea;   // 0: not tracked, 1: client area, 2: non-client area
    int                                                MouseButtonsDown;
    INT64                                              Time;
    INT64                                              TicksPerSecond;
    ImGuiMouseCursor                                   LastMouseCursor;

    ::EventRegistrationToken                           PointerMovedToken;
    ::EventRegistrationToken                           PointerExitedToken;
    ::EventRegistrationToken                           KeyDownToken;
    ::EventRegistrationToken                           KeyUpToken;
    ::EventRegistrationToken                           CharacterReceivedToken;
    ::EventRegistrationToken                           PointerWheelChangedToken;
    ::EventRegistrationToken                           WindowActivatedToken;
    ::EventRegistrationToken                           PointerPressedToken;
    ::EventRegistrationToken                           PointerReleasedToken;

#ifndef IMGUI_IMPL_UWP_DISABLE_GAMEPAD
    bool                        HasGamepad;
    bool                        WantUpdateHasGamepad;
    HMODULE                     XInputDLL;
    PFN_XInputGetCapabilities   XInputGetCapabilities;
    PFN_XInputGetState          XInputGetState;
#endif

    ImGui_ImplUwp_Data()      { memset((void*)this, 0, sizeof(*this)); }
};

typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::PointerEventArgs*> PointerMoved_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::PointerEventArgs*> PointerExited_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::KeyEventArgs*> KeyDown_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::CharacterReceivedEventArgs*> CharacterReceived_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::PointerEventArgs*> PointerWheelChanged_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::PointerEventArgs*> PointerPressed_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<::IInspectable*, ABI::Windows::UI::Core::PointerEventArgs*> PointerReleased_Callback;
int PointerMoved(::IInspectable*, ABI::Windows::UI::Core::IPointerEventArgs*);
int PointerExited(::IInspectable*, ABI::Windows::UI::Core::IPointerEventArgs*);
int KeyDown(::IInspectable*, ABI::Windows::UI::Core::IKeyEventArgs*);
int KeyUp(::IInspectable*, ABI::Windows::UI::Core::IKeyEventArgs*);
int CharacterReceived(::IInspectable*, ABI::Windows::UI::Core::ICharacterReceivedEventArgs*);
int PointerWheelChanged(::IInspectable*, ABI::Windows::UI::Core::IPointerEventArgs*);
int PointerPressed(::IInspectable*, ABI::Windows::UI::Core::IPointerEventArgs*);
int PointerReleased(::IInspectable*, ABI::Windows::UI::Core::IPointerEventArgs*);

typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*> WindowPointerMoved_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*> WindowPointerExited_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::KeyEventArgs*> WindowKeyDown_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::CharacterReceivedEventArgs*> WindowCharacterReceived_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*> WindowPointerWheelChanged_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*> WindowPointerPressed_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*> WindowPointerReleased_Callback;
typedef ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::WindowActivatedEventArgs*> WindowActivated_Callback;
int WindowActivated(ABI::Windows::UI::Core::ICoreWindow*, ABI::Windows::UI::Core::IWindowActivatedEventArgs*);

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static ImGui_ImplUwp_Data* ImGui_ImplUwp_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplUwp_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

static ABI::Windows::UI::Core::ICoreWindow* ImGui_ImplUwp_GetCoreWindowForCurrentThread()
{
    ComPtr<ABI::Windows::UI::Core::ICoreWindowStatic> windowStatic;
    ABI::Windows::UI::Core::ICoreWindow* window = nullptr;

    Windows::Foundation::GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(), &windowStatic);

    if (windowStatic->GetForCurrentThread(&window) != S_OK)
        return nullptr;

	return window;
}

// Functions
static bool ImGui_ImplUwp_InitEx(ABI::Windows::UI::Core::ICoreWindow* core_window, ABI::Windows::UI::Xaml::Controls::ISwapChainPanel* swapchain_panel, bool is_for_current_view)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    INT64 perf_frequency, perf_counter;
    if (!::QueryPerformanceFrequency((LARGE_INTEGER*)&perf_frequency))
        return false;
    if (!::QueryPerformanceCounter((LARGE_INTEGER*)&perf_counter))
        return false;

    // Setup backend capabilities flags
    ImGui_ImplUwp_Data* bd = IM_NEW(ImGui_ImplUwp_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = "imgui_impl_uwp";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

    // Clipboard support
#if !defined(IMGUI_DISABLE_UWP_DEFAULT_CLIPBOARD_FUNCTIONS)
    io.GetClipboardTextFn = ImGui_ImplUwp_GetClipboardTextFn;
    io.SetClipboardTextFn = ImGui_ImplUwp_SetClipboardTextFn;
#endif

    if (core_window && !is_for_current_view)
        core_window->AddRef();

    if (swapchain_panel)
    {
        swapchain_panel->AddRef();
		swapchain_panel->QueryInterface(&bd->SwapChainPanelElement);
    }

    bd->CoreWindow = core_window;
    bd->SwapChainPanel = swapchain_panel;
    bd->TicksPerSecond = perf_frequency;
    bd->Time = perf_counter;
    bd->LastMouseCursor = ImGuiMouseCursor_COUNT;

    // Set platform dependent data in viewport
    ImGui::GetMainViewport()->PlatformHandleRaw = (void*)core_window;

    // Dynamically load XInput library
#ifndef IMGUI_IMPL_UWP_DISABLE_GAMEPAD
    bd->WantUpdateHasGamepad = true;
    const char* xinput_dll_names[] =
    {
        "xinput1_4.dll",   // Windows 8+
        "xinput1_3.dll",   // DirectX SDK
        "xinput9_1_0.dll", // Windows Vista, Windows 7
        "xinput1_2.dll",   // DirectX SDK
        "xinput1_1.dll",   // DirectX SDK
        "xinputuap.dll"    // OneCoreUAP
    };
    for (int n = 0; n < IM_ARRAYSIZE(xinput_dll_names); n++)
        if (HMODULE dll = ::LoadLibraryA(xinput_dll_names[n]))
        {
            bd->XInputDLL = dll;
            bd->XInputGetCapabilities = (PFN_XInputGetCapabilities)::GetProcAddress(dll, "XInputGetCapabilities");
            bd->XInputGetState = (PFN_XInputGetState)::GetProcAddress(dll, "XInputGetState");
            break;
        }
#endif // IMGUI_IMPL_UWP_DISABLE_GAMEPAD

    /*ComPtr<::IUnknown> controlInput;

	auto winUI = LoadLibraryW(L"Windows.UI.dll");
	auto SystemCreateControlInput = (PFN_CreateControlInput)GetProcAddress(winUI, "CreateControlInput");
	auto SystemCreateControlInputEx = (PFN_CreateControlInputEx)GetProcAddress(winUI, "CreateControlInputEx");

    if (core_window)
    {
        bd->CoreWindow->add_Activated(Callback<WindowActivated_Callback>(WindowActivated).Get(), &bd->WindowActivatedToken);
        SystemCreateControlInputEx(core_window, ABI::Windows::UI::Core::IID_ICoreInputSourceBase, &controlInput);
    }*/

    if (core_window)
    {
        bd->CoreWindow->add_PointerMoved(Callback<WindowPointerMoved_Callback>(PointerMoved).Get(), &bd->PointerMovedToken);
        bd->CoreWindow->add_PointerExited(Callback<WindowPointerExited_Callback>(PointerExited).Get(), &bd->PointerExitedToken);
        bd->CoreWindow->add_PointerWheelChanged(Callback<WindowPointerWheelChanged_Callback>(PointerWheelChanged).Get(), &bd->PointerWheelChangedToken);
        bd->CoreWindow->add_PointerPressed(Callback<WindowPointerPressed_Callback>(PointerPressed).Get(), &bd->PointerPressedToken);
        bd->CoreWindow->add_PointerReleased(Callback<WindowPointerReleased_Callback>(PointerReleased).Get(), &bd->PointerReleasedToken);
        bd->CoreWindow->add_KeyDown(Callback<WindowKeyDown_Callback>(KeyDown).Get(), &bd->KeyDownToken);
        bd->CoreWindow->add_KeyUp(Callback<WindowKeyDown_Callback>(KeyUp).Get(), &bd->KeyUpToken);
        bd->CoreWindow->add_CharacterReceived(Callback<WindowCharacterReceived_Callback>(CharacterReceived).Get(), &bd->CharacterReceivedToken);
        bd->CoreWindow->add_Activated(Callback<WindowActivated_Callback>(WindowActivated).Get(), &bd->WindowActivatedToken);
    }
    else if (swapchain_panel)
    {
        ComPtr<ABI::Windows::UI::Core::ICoreInputSourceBase> controlInput;

        auto winUI = LoadLibraryW(L"Windows.UI.dll");
        auto SystemCreateControlInput = (PFN_CreateControlInput)GetProcAddress(winUI, "CreateControlInput");

        HRESULT result = SystemCreateControlInput(ABI::Windows::UI::Core::IID_ICoreInputSourceBase, &controlInput);
        if (SUCCEEDED(result))
        {
			controlInput->put_IsInputEnabled(true);

			ComPtr<ISystemCoreInputInterop> coreInputInterop;
            if (SUCCEEDED(controlInput.As(&coreInputInterop)))
            {
				ComPtr<ABI::Windows::UI::Xaml::Hosting::IElementCompositionPreviewStatics> elementCompositionPreviewStatics;
				result = Windows::Foundation::GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Hosting_ElementCompositionPreview).Get(), &elementCompositionPreviewStatics);

                if (SUCCEEDED(result))
                {
                    ComPtr<ABI::Windows::UI::Xaml::IUIElement> uiElement;
                    swapchain_panel->QueryInterface(uiElement.GetAddressOf());

					ComPtr<ABI::Windows::UI::Composition::IVisual> visual;
					elementCompositionPreviewStatics->GetElementVisual(uiElement.Get(), visual.GetAddressOf());

					result = coreInputInterop->SetInputSource(visual.Get());

                    if 
                    (
                        SUCCEEDED(result)
                        && SUCCEEDED(controlInput->QueryInterface(&bd->PointerInputSource))
                        && SUCCEEDED(controlInput->QueryInterface(&bd->KeyboardInputSource))
                    )
                    {
                        bd->PointerInputSource->add_PointerMoved(Callback<PointerMoved_Callback>(PointerMoved).Get(), &bd->PointerMovedToken);
                        bd->PointerInputSource->add_PointerExited(Callback<PointerExited_Callback>(PointerExited).Get(), &bd->PointerExitedToken);
                        bd->PointerInputSource->add_PointerWheelChanged(Callback<PointerWheelChanged_Callback>(PointerWheelChanged).Get(), &bd->PointerWheelChangedToken);
                        bd->PointerInputSource->add_PointerPressed(Callback<PointerPressed_Callback>(PointerPressed).Get(), &bd->PointerPressedToken);
                        bd->PointerInputSource->add_PointerReleased(Callback<PointerReleased_Callback>(PointerReleased).Get(), &bd->PointerReleasedToken);

                        //bd->KeyboardInputSource->add_KeyDown(Callback<KeyDown_Callback>(KeyDown).Get(), &bd->KeyDownToken);
                        //bd->KeyboardInputSource->add_KeyUp(Callback<KeyDown_Callback>(KeyUp).Get(), &bd->KeyUpToken);
                        //bd->KeyboardInputSource->add_CharacterReceived(Callback<CharacterReceived_Callback>(CharacterReceived).Get(), &bd->CharacterReceivedToken);

                        ComPtr<ABI::Windows::UI::Core::ICoreWindow> window = ImGui_ImplUwp_GetCoreWindowForCurrentThread();
                        if (window.Get())
                        {
                            window->add_KeyDown(Callback<WindowKeyDown_Callback>(KeyDown).Get(), &bd->KeyDownToken);
                            window->add_KeyUp(Callback<WindowKeyDown_Callback>(KeyUp).Get(), &bd->KeyUpToken);
                            window->add_CharacterReceived(Callback<WindowCharacterReceived_Callback>(CharacterReceived).Get(), &bd->CharacterReceivedToken);
                        }
                    }
                }
            }
        }
    }

    return true;
}

IMGUI_IMPL_API bool ImGui_ImplUwp_Init(void* core_window)
{
    return ImGui_ImplUwp_InitEx((ABI::Windows::UI::Core::ICoreWindow*)core_window, nullptr, false);
}

IMGUI_IMPL_API bool ImGui_ImplUwp_InitForCurrentView()
{
    auto window = ImGui_ImplUwp_GetCoreWindowForCurrentThread();
	if (!window)
		return false;

    return ImGui_ImplUwp_InitEx(window, nullptr, true);
}

IMGUI_IMPL_API bool ImGui_ImplUwp_InitForSwapChainPanel(void* swapchain_panel)
{
    return ImGui_ImplUwp_InitEx(nullptr, (ABI::Windows::UI::Xaml::Controls::ISwapChainPanel*)swapchain_panel, false);
}

void ImGui_ImplUwp_Shutdown()
{
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    // Unload XInput library
#ifndef IMGUI_IMPL_UWP_DISABLE_GAMEPAD
    if (bd->XInputDLL)
        ::FreeLibrary(bd->XInputDLL);
#endif // IMGUI_IMPL_UWP_DISABLE_GAMEPAD

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);

    if (bd->CoreWindow)
    {
        bd->CoreWindow->Release();

        bd->CoreWindow->remove_PointerMoved(bd->PointerMovedToken);
        bd->CoreWindow->remove_PointerExited(bd->PointerExitedToken);
        bd->CoreWindow->remove_KeyDown(bd->KeyDownToken);
        bd->CoreWindow->remove_KeyUp(bd->KeyUpToken);
        bd->CoreWindow->remove_CharacterReceived(bd->CharacterReceivedToken);
        bd->CoreWindow->remove_PointerWheelChanged(bd->PointerWheelChangedToken);
        bd->CoreWindow->remove_Activated(bd->WindowActivatedToken);
        bd->CoreWindow->remove_PointerPressed(bd->PointerPressedToken);
        bd->CoreWindow->remove_PointerReleased(bd->PointerReleasedToken);
    }
	else if (bd->SwapChainPanel)
	{
		bd->PointerInputSource->remove_PointerMoved(bd->PointerMovedToken);
		bd->PointerInputSource->remove_PointerExited(bd->PointerExitedToken);
		bd->PointerInputSource->remove_PointerWheelChanged(bd->PointerWheelChangedToken);
		bd->PointerInputSource->remove_PointerPressed(bd->PointerPressedToken);
		bd->PointerInputSource->remove_PointerReleased(bd->PointerReleasedToken);

		//bd->KeyboardInputSource->remove_KeyDown(bd->KeyDownToken);
		//bd->KeyboardInputSource->remove_KeyUp(bd->KeyUpToken);
		//bd->KeyboardInputSource->remove_CharacterReceived(bd->CharacterReceivedToken);

        ComPtr<ABI::Windows::UI::Core::ICoreWindow> window = ImGui_ImplUwp_GetCoreWindowForCurrentThread();
        if (window.Get())
        {
            window->remove_KeyDown(bd->KeyDownToken);
            window->remove_KeyUp(bd->KeyUpToken);
            window->remove_CharacterReceived(bd->CharacterReceivedToken);
        }

		bd->PointerInputSource->Release();
		bd->KeyboardInputSource->Release();
		bd->SwapChainPanel->Release();
		bd->SwapChainPanelElement->Release();
	}

    IM_DELETE(bd);
}

static bool ImGui_ImplUwp_UpdateMouseCursor()
{
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
        return false;

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        if (bd->CoreWindow)
            bd->CoreWindow->put_PointerCursor(nullptr);
        else if (bd->PointerInputSource)
			bd->PointerInputSource->put_PointerCursor(nullptr);
    }
    else
    {
        // Show OS mouse cursor
        ComPtr<ABI::Windows::UI::Core::ICoreCursor> cursor;
        ComPtr<ABI::Windows::UI::Core::ICoreCursorFactory> cursorFactory;
        Windows::Foundation::GetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Core_CoreCursor).Get(), &cursorFactory);

        HRESULT result = 0;

        switch (imgui_cursor)
        {
        case ImGuiMouseCursor_Arrow:        result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_Arrow, 0, &cursor); break;
        case ImGuiMouseCursor_TextInput:    result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_IBeam, 0, &cursor); break;
        case ImGuiMouseCursor_ResizeAll:    result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_SizeAll, 0, &cursor); break;
        case ImGuiMouseCursor_ResizeEW:     result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_SizeWestEast, 0, &cursor); break;
        case ImGuiMouseCursor_ResizeNS:     result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_SizeNorthSouth, 0, &cursor); break;
        case ImGuiMouseCursor_ResizeNESW:   result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_SizeNortheastSouthwest, 0, &cursor); break;
        case ImGuiMouseCursor_ResizeNWSE:   result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_SizeNorthwestSoutheast, 0, &cursor); break;
        case ImGuiMouseCursor_Hand:         result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_Hand, 0, &cursor); break;
        case ImGuiMouseCursor_NotAllowed:   result = cursorFactory->CreateCursor(ABI::Windows::UI::Core::CoreCursorType_UniversalNo, 0, &cursor); break;
        }

        if (result == S_OK)
        {
			if (bd->CoreWindow)
                bd->CoreWindow->put_PointerCursor(cursor.Get());
			else if (bd->PointerInputSource)
				bd->PointerInputSource->put_PointerCursor(cursor.Get());
        }
    }
    return true;
}

static bool IsVkDown(int vk)
{
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();

    ABI::Windows::UI::Core::CoreVirtualKeyStates states = ABI::Windows::UI::Core::CoreVirtualKeyStates::CoreVirtualKeyStates_None;

	if (bd->CoreWindow)
        bd->CoreWindow->GetKeyState((ABI::Windows::System::VirtualKey)vk, &states);
	else if (bd->KeyboardInputSource)
		bd->KeyboardInputSource->GetCurrentKeyState((ABI::Windows::System::VirtualKey)vk, &states);

    return (states & ABI::Windows::UI::Core::CoreVirtualKeyStates::CoreVirtualKeyStates_Down) != 0;
}

static void ImGui_ImplUwp_AddKeyEvent(ImGuiKey key, bool down, int native_keycode, int native_scancode = -1)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, native_keycode, native_scancode); // To support legacy indexing (<1.87 user code)
    IM_UNUSED(native_scancode);
}

static void ImGui_ImplUwp_ProcessKeyEventsWorkarounds()
{
    // Left & right Shift keys: when both are pressed together, Windows tend to not generate the WM_KEYUP event for the first released one.
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) && !IsVkDown(VK_LSHIFT))
        ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftShift, false, VK_LSHIFT);
    if (ImGui::IsKeyDown(ImGuiKey_RightShift) && !IsVkDown(VK_RSHIFT))
        ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightShift, false, VK_RSHIFT);

    // Sometimes WM_KEYUP for Win key is not passed down to the app (e.g. for Win+V on some setups, according to GLFW).
    if (ImGui::IsKeyDown(ImGuiKey_LeftSuper) && !IsVkDown(VK_LWIN))
        ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftSuper, false, VK_LWIN);
    if (ImGui::IsKeyDown(ImGuiKey_RightSuper) && !IsVkDown(VK_RWIN))
        ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightSuper, false, VK_RWIN);
}

static void ImGui_ImplUwp_UpdateKeyModifiers()
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, IsVkDown(VK_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsVkDown(VK_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsVkDown(VK_MENU));
    io.AddKeyEvent(ImGuiMod_Super, IsVkDown(VK_APPS));
}

static void ImGui_ImplUwp_UpdateMouseData()
{
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();
    ImGuiIO& io = ImGui::GetIO();
    //IM_ASSERT(bd->CoreWindow != 0);

    if (bd->CoreWindow)
    {
        bool is_app_focused;

        ComPtr<ABI::Windows::UI::Core::ICoreWindow5> coreWindow5;
        HRESULT hr = bd->CoreWindow->QueryInterface(coreWindow5.GetAddressOf());

        if (hr == S_OK)
        {
            ABI::Windows::UI::Core::CoreWindowActivationMode mode;
            coreWindow5->get_ActivationMode(&mode);

            is_app_focused = mode == ABI::Windows::UI::Core::CoreWindowActivationMode::CoreWindowActivationMode_ActivatedInForeground;
        }
        else
        {
            boolean inputEnabled;
            boolean visible;

            bd->CoreWindow->get_IsInputEnabled(&inputEnabled);
            bd->CoreWindow->get_Visible(&visible);

            is_app_focused = visible && inputEnabled;
        }

        if (is_app_focused)
        {
            // (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
            if (io.WantSetMousePos)
            {
                ComPtr<ABI::Windows::UI::Core::ICoreWindow2> coreWindow2;
                hr = bd->CoreWindow->QueryInterface(coreWindow2.GetAddressOf());

                if (hr == S_OK)
                {
                    ABI::Windows::Foundation::Point pos = { (int)io.MousePos.x / io.DisplayFramebufferScale.x, (int)io.MousePos.y / io.DisplayFramebufferScale.y };
                    coreWindow2->put_PointerPosition(pos);
                }
            }
        }
    } 
}

// Gamepad navigation mapping
static void ImGui_ImplUwp_UpdateGamepads()
{
#ifndef IMGUI_IMPL_UWP_DISABLE_GAMEPAD
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();
    //if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) // FIXME: Technically feeding gamepad shouldn't depend on this now that they are regular inputs.
    //    return;

    // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
    // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
    if (bd->WantUpdateHasGamepad)
    {
        XINPUT_CAPABILITIES caps = {};
        bd->HasGamepad = bd->XInputGetCapabilities ? (bd->XInputGetCapabilities(0, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) : false;
        bd->WantUpdateHasGamepad = false;
    }

    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    XINPUT_STATE xinput_state;
    XINPUT_GAMEPAD& gamepad = xinput_state.Gamepad;
    if (!bd->HasGamepad || bd->XInputGetState == nullptr || bd->XInputGetState(0, &xinput_state) != ERROR_SUCCESS)
        return;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    #define IM_SATURATE(V)                      (V < 0.0f ? 0.0f : V > 1.0f ? 1.0f : V)
    #define MAP_BUTTON(KEY_NO, BUTTON_ENUM)     { io.AddKeyEvent(KEY_NO, (gamepad.wButtons & BUTTON_ENUM) != 0); }
    #define MAP_ANALOG(KEY_NO, VALUE, V0, V1)   { float vn = (float)(VALUE - V0) / (float)(V1 - V0); io.AddKeyAnalogEvent(KEY_NO, vn > 0.10f, IM_SATURATE(vn)); }
    MAP_BUTTON(ImGuiKey_GamepadStart,           XINPUT_GAMEPAD_START);
    MAP_BUTTON(ImGuiKey_GamepadBack,            XINPUT_GAMEPAD_BACK);
    MAP_BUTTON(ImGuiKey_GamepadFaceLeft,        XINPUT_GAMEPAD_X);
    MAP_BUTTON(ImGuiKey_GamepadFaceRight,       XINPUT_GAMEPAD_B);
    MAP_BUTTON(ImGuiKey_GamepadFaceUp,          XINPUT_GAMEPAD_Y);
    MAP_BUTTON(ImGuiKey_GamepadFaceDown,        XINPUT_GAMEPAD_A);
    MAP_BUTTON(ImGuiKey_GamepadDpadLeft,        XINPUT_GAMEPAD_DPAD_LEFT);
    MAP_BUTTON(ImGuiKey_GamepadDpadRight,       XINPUT_GAMEPAD_DPAD_RIGHT);
    MAP_BUTTON(ImGuiKey_GamepadDpadUp,          XINPUT_GAMEPAD_DPAD_UP);
    MAP_BUTTON(ImGuiKey_GamepadDpadDown,        XINPUT_GAMEPAD_DPAD_DOWN);
    MAP_BUTTON(ImGuiKey_GamepadL1,              XINPUT_GAMEPAD_LEFT_SHOULDER);
    MAP_BUTTON(ImGuiKey_GamepadR1,              XINPUT_GAMEPAD_RIGHT_SHOULDER);
    MAP_ANALOG(ImGuiKey_GamepadL2,              gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
    MAP_ANALOG(ImGuiKey_GamepadR2,              gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
    MAP_BUTTON(ImGuiKey_GamepadL3,              XINPUT_GAMEPAD_LEFT_THUMB);
    MAP_BUTTON(ImGuiKey_GamepadR3,              XINPUT_GAMEPAD_RIGHT_THUMB);
    MAP_ANALOG(ImGuiKey_GamepadLStickLeft,      gamepad.sThumbLX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(ImGuiKey_GamepadLStickRight,     gamepad.sThumbLX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadLStickUp,        gamepad.sThumbLY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadLStickDown,      gamepad.sThumbLY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(ImGuiKey_GamepadRStickLeft,      gamepad.sThumbRX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(ImGuiKey_GamepadRStickRight,     gamepad.sThumbRX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadRStickUp,        gamepad.sThumbRY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadRStickDown,      gamepad.sThumbRY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    #undef MAP_BUTTON
    #undef MAP_ANALOG
#endif // #ifndef IMGUI_IMPL_UWP_DISABLE_GAMEPAD
}

void    ImGui_ImplUwp_NewFrame()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplUwp_Init()?");

    // Setup display size (every frame to accommodate for window resizing)

	if (bd->CoreWindow)
	{
		ABI::Windows::Foundation::Rect rect = { 0, 0, 0, 0 };
		bd->CoreWindow->get_Bounds(&rect);
		io.DisplaySize = ImVec2(rect.Width * io.DisplayFramebufferScale.x, rect.Height * io.DisplayFramebufferScale.y);
	}
	else if (bd->SwapChainPanelElement)
	{
        DOUBLE w, h;
		bd->SwapChainPanelElement->get_ActualHeight(&h);
		bd->SwapChainPanelElement->get_ActualWidth(&w);
		io.DisplaySize = ImVec2(w * io.DisplayFramebufferScale.x, h * io.DisplayFramebufferScale.y);
	}

    // Setup time step
    INT64 current_time = 0;
    ::QueryPerformanceCounter((LARGE_INTEGER*)&current_time);
    io.DeltaTime = (float)(current_time - bd->Time) / bd->TicksPerSecond;
    bd->Time = current_time;

    // Update OS mouse position
    ImGui_ImplUwp_UpdateMouseData();

    // Process workarounds for known Windows key handling issues
    ImGui_ImplUwp_ProcessKeyEventsWorkarounds();

    // Update OS mouse cursor with the cursor requested by imgui
    ImGuiMouseCursor mouse_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (bd->LastMouseCursor != mouse_cursor)
    {
        bd->LastMouseCursor = mouse_cursor;
        ImGui_ImplUwp_UpdateMouseCursor();
    }

    // Update game controllers (if enabled and available)
    ImGui_ImplUwp_UpdateGamepads();
}

// There is no distinct VK_xxx for keypad enter, instead it is VK_RETURN + KF_EXTENDED, we assign it an arbitrary value to make code more readable (VK_ codes go up to 255)
#define IM_VK_KEYPAD_ENTER      (VK_RETURN + 256)

// Map VK_xxx to ImGuiKey_xxx.
static ImGuiKey ImGui_ImplUwp_VirtualKeyToImGuiKey(WPARAM wParam)
{
    switch (wParam)
    {
        case VK_TAB: return ImGuiKey_Tab;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        case VK_PRIOR: return ImGuiKey_PageUp;
        case VK_NEXT: return ImGuiKey_PageDown;
        case VK_HOME: return ImGuiKey_Home;
        case VK_END: return ImGuiKey_End;
        case VK_INSERT: return ImGuiKey_Insert;
        case VK_DELETE: return ImGuiKey_Delete;
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_OEM_7: return ImGuiKey_Apostrophe;
        case VK_OEM_COMMA: return ImGuiKey_Comma;
        case VK_OEM_MINUS: return ImGuiKey_Minus;
        case VK_OEM_PERIOD: return ImGuiKey_Period;
        case VK_OEM_2: return ImGuiKey_Slash;
        case VK_OEM_1: return ImGuiKey_Semicolon;
        case VK_OEM_PLUS: return ImGuiKey_Equal;
        case VK_OEM_4: return ImGuiKey_LeftBracket;
        case VK_OEM_5: return ImGuiKey_Backslash;
        case VK_OEM_6: return ImGuiKey_RightBracket;
        case VK_OEM_3: return ImGuiKey_GraveAccent;
        case VK_CAPITAL: return ImGuiKey_CapsLock;
        case VK_SCROLL: return ImGuiKey_ScrollLock;
        case VK_NUMLOCK: return ImGuiKey_NumLock;
        case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
        case VK_PAUSE: return ImGuiKey_Pause;
        case VK_NUMPAD0: return ImGuiKey_Keypad0;
        case VK_NUMPAD1: return ImGuiKey_Keypad1;
        case VK_NUMPAD2: return ImGuiKey_Keypad2;
        case VK_NUMPAD3: return ImGuiKey_Keypad3;
        case VK_NUMPAD4: return ImGuiKey_Keypad4;
        case VK_NUMPAD5: return ImGuiKey_Keypad5;
        case VK_NUMPAD6: return ImGuiKey_Keypad6;
        case VK_NUMPAD7: return ImGuiKey_Keypad7;
        case VK_NUMPAD8: return ImGuiKey_Keypad8;
        case VK_NUMPAD9: return ImGuiKey_Keypad9;
        case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
        case VK_DIVIDE: return ImGuiKey_KeypadDivide;
        case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case VK_ADD: return ImGuiKey_KeypadAdd;
        case IM_VK_KEYPAD_ENTER: return ImGuiKey_KeypadEnter;
        case VK_LSHIFT: return ImGuiKey_LeftShift;
        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
        case VK_LMENU: return ImGuiKey_LeftAlt;
        case VK_LWIN: return ImGuiKey_LeftSuper;
        case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_RCONTROL: return ImGuiKey_RightCtrl;
        case VK_RMENU: return ImGuiKey_RightAlt;
        case VK_RWIN: return ImGuiKey_RightSuper;
        case VK_APPS: return ImGuiKey_Menu;
        case '0': return ImGuiKey_0;
        case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4;
        case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6;
        case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;
        case 'A': return ImGuiKey_A;
        case 'B': return ImGuiKey_B;
        case 'C': return ImGuiKey_C;
        case 'D': return ImGuiKey_D;
        case 'E': return ImGuiKey_E;
        case 'F': return ImGuiKey_F;
        case 'G': return ImGuiKey_G;
        case 'H': return ImGuiKey_H;
        case 'I': return ImGuiKey_I;
        case 'J': return ImGuiKey_J;
        case 'K': return ImGuiKey_K;
        case 'L': return ImGuiKey_L;
        case 'M': return ImGuiKey_M;
        case 'N': return ImGuiKey_N;
        case 'O': return ImGuiKey_O;
        case 'P': return ImGuiKey_P;
        case 'Q': return ImGuiKey_Q;
        case 'R': return ImGuiKey_R;
        case 'S': return ImGuiKey_S;
        case 'T': return ImGuiKey_T;
        case 'U': return ImGuiKey_U;
        case 'V': return ImGuiKey_V;
        case 'W': return ImGuiKey_W;
        case 'X': return ImGuiKey_X;
        case 'Y': return ImGuiKey_Y;
        case 'Z': return ImGuiKey_Z;
        case VK_F1: return ImGuiKey_F1;
        case VK_F2: return ImGuiKey_F2;
        case VK_F3: return ImGuiKey_F3;
        case VK_F4: return ImGuiKey_F4;
        case VK_F5: return ImGuiKey_F5;
        case VK_F6: return ImGuiKey_F6;
        case VK_F7: return ImGuiKey_F7;
        case VK_F8: return ImGuiKey_F8;
        case VK_F9: return ImGuiKey_F9;
        case VK_F10: return ImGuiKey_F10;
        case VK_F11: return ImGuiKey_F11;
        case VK_F12: return ImGuiKey_F12;
        default: return ImGuiKey_None;
    }
}

static ImGuiMouseSource GetMouseSourceFromDevice(ABI::Windows::Devices::Input::IPointerDevice* device)
{
    ABI::Windows::Devices::Input::PointerDeviceType type;
    device->get_PointerDeviceType(&type);

    if (type == ABI::Windows::Devices::Input::PointerDeviceType::PointerDeviceType_Pen)
        return ImGuiMouseSource_Pen;
    if (type == ABI::Windows::Devices::Input::PointerDeviceType::PointerDeviceType_Touch)
        return ImGuiMouseSource_TouchScreen;
    return ImGuiMouseSource_Mouse;
}

int PointerMoved(::IInspectable* sender, ABI::Windows::UI::Core::IPointerEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();

    ABI::Windows::Foundation::Point point;
    ComPtr<ABI::Windows::UI::Input::IPointerPoint> pointerPoint;
    ComPtr<ABI::Windows::Devices::Input::IPointerDevice> pointerDevice;

    if (args->get_CurrentPoint(&pointerPoint) != S_OK)
        return 0;

    if (pointerPoint->get_PointerDevice(&pointerDevice) != S_OK)
        return 0;

    pointerPoint->get_Position(&point);

    ImGuiMouseSource mouse_source = GetMouseSourceFromDevice(pointerDevice.Get());
    io.AddMouseSourceEvent(mouse_source);
    io.AddMousePosEvent(point.X * io.DisplayFramebufferScale.x, point.Y * io.DisplayFramebufferScale.y);

    return 0;
}

int PointerExited(::IInspectable* sender, ABI::Windows::UI::Core::IPointerEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();

    bd->MouseTrackedArea = 0;
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    return 0;
}

int KeyDown(::IInspectable* sender, ABI::Windows::UI::Core::IKeyEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();

    ABI::Windows::System::VirtualKey key;
    ABI::Windows::UI::Core::CorePhysicalKeyStatus keyStatus;
    args->get_VirtualKey(&key);
    args->get_KeyStatus(&keyStatus);

    const bool is_key_down = true;
    if (key < 256)
    {
        // Submit modifiers
        ImGui_ImplUwp_UpdateKeyModifiers();

        int vk = (int)key;
        if ((key == VK_RETURN) && keyStatus.IsExtendedKey)
            vk = IM_VK_KEYPAD_ENTER;

        // Submit key event
        const ImGuiKey key = ImGui_ImplUwp_VirtualKeyToImGuiKey(vk);
        const int scancode = keyStatus.ScanCode;
        if (key != ImGuiKey_None)
            ImGui_ImplUwp_AddKeyEvent(key, is_key_down, vk, scancode);

        // Submit individual left/right modifier events
        if (vk == VK_SHIFT)
        {
            // Important: Shift keys tend to get stuck when pressed together, missing key-up events are corrected in ImGui_ImplUwp_ProcessKeyEventsWorkarounds()
            if (IsVkDown(VK_LSHIFT) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftShift, is_key_down, VK_LSHIFT, scancode); }
            if (IsVkDown(VK_RSHIFT) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightShift, is_key_down, VK_RSHIFT, scancode); }
        }
        else if (vk == VK_CONTROL)
        {
            if (IsVkDown(VK_LCONTROL) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftCtrl, is_key_down, VK_LCONTROL, scancode); }
            if (IsVkDown(VK_RCONTROL) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightCtrl, is_key_down, VK_RCONTROL, scancode); }
        }
        else if (vk == VK_MENU || keyStatus.IsMenuKeyDown)
        {
            if (IsVkDown(VK_LMENU) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftAlt, is_key_down, VK_LMENU, scancode); }
            if (IsVkDown(VK_RMENU) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightAlt, is_key_down, VK_RMENU, scancode); }

            if (!IsVkDown(VK_LMENU) && !IsVkDown(VK_RMENU)) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftAlt, is_key_down, VK_LMENU, scancode); }
        }
    }

    return 0;
}

int KeyUp(::IInspectable* sender, ABI::Windows::UI::Core::IKeyEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();

    ABI::Windows::System::VirtualKey key;
    ABI::Windows::UI::Core::CorePhysicalKeyStatus keyStatus;
    args->get_VirtualKey(&key);
    args->get_KeyStatus(&keyStatus);

    const bool is_key_down = false;
    if (key < 256)
    {
        // Submit modifiers
        ImGui_ImplUwp_UpdateKeyModifiers();

        int vk = (int)key;
        if ((key == VK_RETURN) && keyStatus.IsExtendedKey)
            vk = IM_VK_KEYPAD_ENTER;

        // Submit key event
        const ImGuiKey key = ImGui_ImplUwp_VirtualKeyToImGuiKey(vk);
        const int scancode = keyStatus.ScanCode;
        if (key != ImGuiKey_None)
            ImGui_ImplUwp_AddKeyEvent(key, is_key_down, vk, scancode);

        // Submit individual left/right modifier events
        if (vk == VK_SHIFT)
        {
            // Important: Shift keys tend to get stuck when pressed together, missing key-up events are corrected in ImGui_ImplUwp_ProcessKeyEventsWorkarounds()
            if (IsVkDown(VK_LSHIFT) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftShift, is_key_down, VK_LSHIFT, scancode); }
            if (IsVkDown(VK_RSHIFT) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightShift, is_key_down, VK_RSHIFT, scancode); }
        }
        else if (vk == VK_CONTROL)
        {
            if (IsVkDown(VK_LCONTROL) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftCtrl, is_key_down, VK_LCONTROL, scancode); }
            if (IsVkDown(VK_RCONTROL) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightCtrl, is_key_down, VK_RCONTROL, scancode); }
        }
        else if (vk == VK_MENU || !keyStatus.IsMenuKeyDown)
        {
            if (IsVkDown(VK_LMENU) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftAlt, is_key_down, VK_LMENU, scancode); }
            if (IsVkDown(VK_RMENU) == is_key_down) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_RightAlt, is_key_down, VK_RMENU, scancode); }

            if (!IsVkDown(VK_LMENU) && !IsVkDown(VK_RMENU)) { ImGui_ImplUwp_AddKeyEvent(ImGuiKey_LeftAlt, is_key_down, VK_LMENU, scancode); }
        }
    }

    return 0;
}

unsigned short UTF32ToUTF16(UINT32 utf32)
{
    unsigned int h, l;

    if (utf32 < 0x10000)
    {
        h = 0;
        l = utf32;
        return utf32;
    }
    unsigned int t = utf32 - 0x10000;
    h = (((t << 12) >> 22) + 0xD800);
    l = (((t << 22) >> 22) + 0xDC00);
    unsigned short ret = ((h << 16) | (l & 0x0000FFFF));
    return ret;
}

int CharacterReceived(::IInspectable* sender, ABI::Windows::UI::Core::ICharacterReceivedEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();

    UINT32 code;
    args->get_KeyCode(&code);

    io.AddInputCharacterUTF16(UTF32ToUTF16(code));

    return 0;
}

int PointerWheelChanged(::IInspectable* sender, ABI::Windows::UI::Core::IPointerEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();

    INT32 mouseWheelDelta;
    boolean isHorizontalMouseWheel;
    ComPtr<ABI::Windows::UI::Input::IPointerPoint> pointerPoint;
    ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> pointerPointProps;

    if (args->get_CurrentPoint(&pointerPoint) != S_OK)
        return 0;

    if (pointerPoint->get_Properties(&pointerPointProps) != S_OK)
        return 0;

    pointerPointProps->get_IsHorizontalMouseWheel(&isHorizontalMouseWheel);
    pointerPointProps->get_MouseWheelDelta(&mouseWheelDelta);

    if (isHorizontalMouseWheel)
        io.AddMouseWheelEvent(-(float) mouseWheelDelta / (float)WHEEL_DELTA, 0.0f);
    else
        io.AddMouseWheelEvent(0.0f, (float)mouseWheelDelta / (float)WHEEL_DELTA);

    return 0;
}

int WindowActivated(ABI::Windows::UI::Core::ICoreWindow* sender, ABI::Windows::UI::Core::IWindowActivatedEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();

    ABI::Windows::UI::Core::CoreWindowActivationState state;
    args->get_WindowActivationState(&state);

    io.AddFocusEvent(state != ABI::Windows::UI::Core::CoreWindowActivationState::CoreWindowActivationState_Deactivated);

    return 0;
}

int PointerPressed(::IInspectable* sender, ABI::Windows::UI::Core::IPointerEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();

    ComPtr<ABI::Windows::UI::Input::IPointerPoint> pointerPoint;
    ComPtr<ABI::Windows::Devices::Input::IPointerDevice> pointerDevice;
    ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> pointerPointProps;

    boolean lBtn;
    boolean rBtn;
    boolean mBtn;
    boolean x1Btn;
    boolean x2Btn;

    if (args->get_CurrentPoint(&pointerPoint) != S_OK)
        return 0;

    if (pointerPoint->get_Properties(&pointerPointProps) != S_OK)
        return 0;

    if (pointerPoint->get_PointerDevice(&pointerDevice) != S_OK)
        return 0;

    pointerPointProps->get_IsLeftButtonPressed(&lBtn);
    pointerPointProps->get_IsRightButtonPressed(&rBtn);
    pointerPointProps->get_IsMiddleButtonPressed(&mBtn);
    pointerPointProps->get_IsXButton1Pressed(&x1Btn);
    pointerPointProps->get_IsXButton2Pressed(&x2Btn);

    int button = 0;

    if (x2Btn && (bd->MouseButtonsDown & (1 << 4)) == 0) button = 4;
    if (x1Btn && (bd->MouseButtonsDown & (1 << 3)) == 0) button = 3;
    if (mBtn && (bd->MouseButtonsDown & (1 << 2)) == 0) button = 2;
    if (rBtn && (bd->MouseButtonsDown & (1 << 1)) == 0) button = 1;
    if (lBtn && (bd->MouseButtonsDown & (1 << 0)) == 0) button = 0;

    ImGuiMouseSource mouse_source = GetMouseSourceFromDevice(pointerDevice.Get());

    bd->MouseButtonsDown |= 1 << button;
    io.AddMouseSourceEvent(mouse_source);
    io.AddMouseButtonEvent(button, true);

    return 0;
}

int PointerReleased(::IInspectable* sender, ABI::Windows::UI::Core::IPointerEventArgs* args)
{
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplUwp_Data* bd = ImGui_ImplUwp_GetBackendData();

    ComPtr<ABI::Windows::UI::Input::IPointerPoint> pointerPoint;
    ComPtr<ABI::Windows::Devices::Input::IPointerDevice> pointerDevice;
    ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> pointerPointProps;

    boolean lBtn;
    boolean rBtn;
    boolean mBtn;
    boolean x1Btn;
    boolean x2Btn;

    if (args->get_CurrentPoint(&pointerPoint) != S_OK)
        return 0;

    if (pointerPoint->get_Properties(&pointerPointProps) != S_OK)
        return 0;

    if (pointerPoint->get_PointerDevice(&pointerDevice) != S_OK)
        return 0;

    pointerPointProps->get_IsLeftButtonPressed(&lBtn);
    pointerPointProps->get_IsRightButtonPressed(&rBtn);
    pointerPointProps->get_IsMiddleButtonPressed(&mBtn);
    pointerPointProps->get_IsXButton1Pressed(&x1Btn);
    pointerPointProps->get_IsXButton2Pressed(&x2Btn);

    int button = 0;

    if (!x2Btn && (bd->MouseButtonsDown & (1 << 4)) != 0) button = 4;
    if (!x1Btn && (bd->MouseButtonsDown & (1 << 3)) != 0) button = 3;
    if (!mBtn && (bd->MouseButtonsDown & (1 << 2)) != 0) button = 2;
    if (!rBtn && (bd->MouseButtonsDown & (1 << 1)) != 0) button = 1;
    if (!lBtn && (bd->MouseButtonsDown & (1 << 0)) != 0) button = 0;

    ImGuiMouseSource mouse_source = GetMouseSourceFromDevice(pointerDevice.Get());

    bd->MouseButtonsDown &= ~(1 << button);
    io.AddMouseSourceEvent(mouse_source);
    io.AddMouseButtonEvent(button, false);

    return 0;
}

// UWP clipboard implementation
#if !defined(IMGUI_DISABLE_UWP_DEFAULT_CLIPBOARD_FUNCTIONS)
static const char* ImGui_ImplUwp_GetClipboardTextFn(void* user_data_ctx)
{
    ImGuiContext& g = *(ImGuiContext*)user_data_ctx;
    g.ClipboardHandlerData.clear();

    Microsoft::WRL::ComPtr<ABI::Windows::ApplicationModel::DataTransfer::IClipboardStatics> clipboardStatics;
    Microsoft::WRL::ComPtr<ABI::Windows::ApplicationModel::DataTransfer::IStandardDataFormatsStatics> standardDataFormats;
    Microsoft::WRL::ComPtr<ABI::Windows::ApplicationModel::DataTransfer::IDataPackageView> dataPkgView;
    Windows::Foundation::GetActivationFactory(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard).Get(), &clipboardStatics);
    Windows::Foundation::GetActivationFactory(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_StandardDataFormats).Get(), &standardDataFormats);

    if (clipboardStatics->GetContent(&dataPkgView) != S_OK)
        return NULL;

    Microsoft::WRL::Wrappers::HString hstr;
    boolean containsText;
    standardDataFormats->get_Text(hstr.GetAddressOf());
    dataPkgView->Contains(hstr.Get(), &containsText);
    hstr.Release();

    if (!containsText)
        return NULL;

    Microsoft::WRL::Wrappers::HString text;
    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<HSTRING>> operation;

    if (dataPkgView->GetTextAsync(operation.GetAddressOf()) != S_OK)
        return NULL;

    HRESULT hr;
    Microsoft::WRL::Wrappers::Event opCompleted(CreateEventEx(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, WRITE_OWNER | EVENT_ALL_ACCESS));

    Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperationCompletedHandler<HSTRING>> cb
        = Microsoft::WRL::Callback<Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ABI::Windows::Foundation::IAsyncOperationCompletedHandler<HSTRING>, Microsoft::WRL::FtmBase>>(
            [&opCompleted](ABI::Windows::Foundation::IAsyncOperation<HSTRING>* asyncOperation, AsyncStatus status)->HRESULT
            {
                SetEvent(opCompleted.Get());
                return S_OK;
            });

    operation->put_Completed(cb.Get());
    WaitForSingleObject(opCompleted.Get(), INFINITE);
    hr = operation->GetResults(text.GetAddressOf());

    if (hr != S_OK)
    {
        text.Release();
        return NULL;
    }

    if (const WCHAR* wbuf_global = (const WCHAR*)text.GetRawBuffer(NULL))
    {
        int buf_len = ::WideCharToMultiByte(CP_UTF8, 0, wbuf_global, -1, NULL, 0, NULL, NULL);
        g.ClipboardHandlerData.resize(buf_len);
        ::WideCharToMultiByte(CP_UTF8, 0, wbuf_global, -1, g.ClipboardHandlerData.Data, buf_len, NULL, NULL);
    }

    text.Release();

    return g.ClipboardHandlerData.Data;
}

static void ImGui_ImplUwp_SetClipboardTextFn(void*, const char* text)
{
    Microsoft::WRL::ComPtr<ABI::Windows::ApplicationModel::DataTransfer::IDataPackage> dataPkg;
    Microsoft::WRL::ComPtr<ABI::Windows::ApplicationModel::DataTransfer::IClipboardStatics> clipboardStatics;
    Windows::Foundation::ActivateInstance(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_DataPackage).Get(), &dataPkg);
    Windows::Foundation::GetActivationFactory(Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_ApplicationModel_DataTransfer_Clipboard).Get(), &clipboardStatics);

    HRESULT hr = dataPkg->put_RequestedOperation(ABI::Windows::ApplicationModel::DataTransfer::DataPackageOperation_Copy);
    if (hr != S_OK)
        return;

    const int wbuf_length = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HGLOBAL wbuf_handle = ::GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wbuf_length * sizeof(WCHAR));
    if (wbuf_handle == NULL)
        return;

    WCHAR* wbuf_global = (WCHAR*)::GlobalLock(wbuf_handle);
    ::MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf_global, wbuf_length);
    ::GlobalUnlock(wbuf_handle);

    if (dataPkg->SetText(Microsoft::WRL::Wrappers::HStringReference(wbuf_global).Get()) != S_OK)
    {
        ::GlobalFree(wbuf_handle);
        return;
    }

    clipboardStatics->SetContent(dataPkg.Get());
    ::GlobalFree(wbuf_handle);
}
#endif

//---------------------------------------------------------------------------------------------------------
