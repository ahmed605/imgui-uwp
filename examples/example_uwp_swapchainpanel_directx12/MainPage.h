#pragma once

#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Navigation.h>

#include "MainPage.g.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <imgui.h>
#include <imgui_impl_uwp.h>
#include <imgui_impl_dx12.h>

namespace winrt::example_uwp_swapchainpanel_directx12::implementation
{
    struct MainPage : MainPageT<MainPage>
    {
        MainPage()
        {
        }

		bool m_windowClosed = false;
		bool m_windowVisible = true;

		struct FrameContext
		{
			ID3D12CommandAllocator* CommandAllocator;
			UINT64                  FenceValue;
		};

		static const int NUM_FRAMES_IN_FLIGHT = 3;
		FrameContext m_frameContext[NUM_FRAMES_IN_FLIGHT];
		UINT m_frameIndex = 0;

		static int const NUM_BACK_BUFFERS = 3;
		com_ptr<ID3D12Device> m_pd3dDevice;
		com_ptr<ID3D12DescriptorHeap> m_pd3dRtvDescHeap;
		com_ptr<ID3D12DescriptorHeap> m_pd3dSrvDescHeap;
		com_ptr<ID3D12CommandQueue> m_pd3dCommandQueue;
		com_ptr<ID3D12GraphicsCommandList> m_pd3dCommandList;
		com_ptr<ID3D12Fence> m_fence;
		HANDLE m_fenceEvent;
		UINT64 m_fenceLastSignaledValue;
		com_ptr <IDXGISwapChain3> m_pSwapChain;
		HANDLE m_hSwapChainWaitableObject;
		com_ptr <ID3D12Resource> m_mainRenderTargetResource[NUM_BACK_BUFFERS];
		D3D12_CPU_DESCRIPTOR_HANDLE  m_mainRenderTargetDescriptor[NUM_BACK_BUFFERS];

		UINT m_ResizeWidth = 0, m_ResizeHeight = 0;
		Windows::Foundation::Size m_OldSize;

		float m_scaleX, m_scaleY;

		event_token m_rendeingToken;

		bool m_show_demo_window = true;
		bool m_show_another_window = false;
		ImVec4 m_clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

		void InitDevice(winrt::Windows::UI::Core::CoreWindow const& window, const winrt::Windows::UI::Xaml::Controls::SwapChainPanel& panel);
		void OnSizeChanged(IInspectable const&, winrt::Windows::UI::Xaml::SizeChangedEventArgs const& args);
		void OnVisibiltyChanged(IInspectable const&, winrt::Windows::UI::Core::VisibilityChangedEventArgs const& args);
		void OnWindowClosed(IInspectable const&, winrt::Windows::UI::Core::CoreWindowEventArgs const& args);
		void OnRendering(const IInspectable&, const IInspectable&);
		void OnCompositionScaleChanged(const winrt::Windows::UI::Xaml::Controls::SwapChainPanel&, const IInspectable&);
		void CreateRenderTarget();
		void CleanupRenderTarget();
		void WaitForLastSubmittedFrame();
		FrameContext* WaitForNextFrameResources();

        void swapchain_Loaded(winrt::Windows::Foundation::IInspectable const& sender, winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
	};
}

namespace winrt::example_uwp_swapchainpanel_directx12::factory_implementation
{
    struct MainPage : MainPageT<MainPage, implementation::MainPage>
    {
    };
}
