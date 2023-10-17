#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>

#include <imgui.h>
#include <imgui_impl_uwp.h>
#include <imgui_impl_dx12.h>

#include <d3d12.h>
#include <dxgi1_4.h>

using namespace winrt;

using namespace Windows;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::Display;
using namespace Windows::UI;
using namespace Windows::UI::Core;

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
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

	IFrameworkView CreateView()
	{
		return *this;
	}

	void Initialize(CoreApplicationView const&)
	{
	}

	void Load(hstring const&)
	{
	}

	void Uninitialize()
	{
	}

	void Run()
	{
		CoreWindow window = CoreWindow::GetForCurrentThread();
		CoreDispatcher dispatcher = window.Dispatcher();
		window.Activate();

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);

		// High DPI scaling
		DisplayInformation currentDisplayInformation = DisplayInformation::GetForCurrentView();
		float dpi = currentDisplayInformation.LogicalDpi() / 96.0f;
		io.DisplayFramebufferScale = { dpi, dpi };                // TODO: Handle DPI change

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Setup Platform/Renderer backends
		ImGui_ImplUwp_InitForCurrentView();
		ImGui_ImplDX12_Init(m_pd3dDevice.get(), NUM_FRAMES_IN_FLIGHT,
			DXGI_FORMAT_R8G8B8A8_UNORM, m_pd3dSrvDescHeap.get(),
			m_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
			m_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

		// Our state
		bool show_demo_window = true;
		bool show_another_window = false;
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

		while (!m_windowClosed)
		{
			if (m_windowVisible)
			{
				dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

				// Handle window resize (we don't resize directly in the SizeChanged handler)
				if (m_ResizeWidth != 0 && m_ResizeHeight != 0)
				{
					WaitForLastSubmittedFrame();
					CleanupRenderTarget();
					m_pSwapChain->ResizeBuffers(0, lround(m_ResizeWidth * io.DisplayFramebufferScale.x), lround(m_ResizeHeight * io.DisplayFramebufferScale.y), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
					m_ResizeWidth = m_ResizeHeight = 0;
					CreateRenderTarget();
				}

				// Start the Dear ImGui frame
				ImGui_ImplDX12_NewFrame();
				ImGui_ImplUwp_NewFrame();
				ImGui::NewFrame();

				// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
				if (show_demo_window)
					ImGui::ShowDemoWindow(&show_demo_window);

				// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
				{
					static float f = 0.0f;
					static int counter = 0;

					ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

					ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
					ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
					ImGui::Checkbox("Another Window", &show_another_window);

					ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
					ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

					if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
						counter++;
					ImGui::SameLine();
					ImGui::Text("counter = %d", counter);

					ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
					ImGui::End();
				}

				// 3. Show another simple window.
				if (show_another_window)
				{
					ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
					ImGui::Text("Hello from another window!");
					if (ImGui::Button("Close Me"))
						show_another_window = false;
					ImGui::End();
				}

				// Rendering
				ImGui::Render();

				FrameContext* frameCtx = WaitForNextFrameResources();
				UINT backBufferIdx = m_pSwapChain->GetCurrentBackBufferIndex();
				frameCtx->CommandAllocator->Reset();

				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = m_mainRenderTargetResource[backBufferIdx].get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
				m_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
				m_pd3dCommandList->ResourceBarrier(1, &barrier);

				// Render Dear ImGui graphics
				const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
				m_pd3dCommandList->ClearRenderTargetView(m_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
				m_pd3dCommandList->OMSetRenderTargets(1, &m_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
				
				auto d3dSrvDescHeap = m_pd3dSrvDescHeap.get();
				m_pd3dCommandList->SetDescriptorHeaps(1, &d3dSrvDescHeap);

				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_pd3dCommandList.get());
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				m_pd3dCommandList->ResourceBarrier(1, &barrier);
				m_pd3dCommandList->Close();

				m_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&m_pd3dCommandList);

				m_pSwapChain->Present(0, 0);

				UINT64 fenceValue = m_fenceLastSignaledValue + 1;
				m_pd3dCommandQueue->Signal(m_fence.get(), fenceValue);
				m_fenceLastSignaledValue = fenceValue;
				frameCtx->FenceValue = fenceValue;
			}
			else
			{
				dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
			}
		}

		// Cleanup
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplUwp_Shutdown();
		ImGui::DestroyContext();
	}

	void SetWindow(CoreWindow const& window)
	{
		window.Closed({ this, &App::OnWindowClosed });
		window.SizeChanged({ this, &App::OnSizeChanged });
		window.VisibilityChanged({ this, &App::OnVisibiltyChanged });

		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_2,
			D3D_FEATURE_LEVEL_9_1
		};

		D3D_FEATURE_LEVEL featureLevel;

		winrt::check_hresult(D3D12CreateDevice(
			nullptr,
			D3D_FEATURE_LEVEL_11_1,
			__uuidof(m_pd3dDevice),
			m_pd3dDevice.put_void()
		));

		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			desc.NumDescriptors = NUM_BACK_BUFFERS;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			desc.NodeMask = 1;
			winrt::check_hresult(m_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pd3dRtvDescHeap)));

			SIZE_T rtvDescriptorSize = m_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
			{
				m_mainRenderTargetDescriptor[i] = rtvHandle;
				rtvHandle.ptr += rtvDescriptorSize;
			}
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			desc.NumDescriptors = 1;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			winrt::check_hresult(m_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pd3dSrvDescHeap)));
		}

		{
			D3D12_COMMAND_QUEUE_DESC desc = {};
			desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			desc.NodeMask = 1;
			winrt::check_hresult(m_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_pd3dCommandQueue)));
		}

		for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
			winrt::check_hresult(m_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frameContext[i].CommandAllocator)));

		winrt::check_hresult(m_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&m_pd3dCommandList)));
		winrt::check_hresult(m_pd3dCommandList->Close());

		winrt::check_hresult(m_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		winrt::check_hresult(m_fenceEvent == nullptr);

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };

		swapChainDesc.Width = 0;
		swapChainDesc.Height = 0;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = false;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = NUM_BACK_BUFFERS;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		com_ptr<IDXGIFactory4> dxgiFactory;
		winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.put())));

		com_ptr<IDXGISwapChain1> swapChain1;

		winrt::check_hresult(
			dxgiFactory->CreateSwapChainForCoreWindow(
				m_pd3dCommandQueue.get(),
				winrt::get_unknown(window),
				&swapChainDesc,
				nullptr,
				swapChain1.put()
			)
		);

		winrt::check_hresult(swapChain1->QueryInterface(IID_PPV_ARGS(m_pSwapChain.put())));

		winrt::check_hresult(
			m_pSwapChain->SetMaximumFrameLatency(1)
		);

		m_hSwapChainWaitableObject = m_pSwapChain->GetFrameLatencyWaitableObject();

		CreateRenderTarget();
	}

	void OnSizeChanged(IInspectable const&, WindowSizeChangedEventArgs const& args)
	{
		auto size = args.Size();

		// Prevent unnecessary resize
		if (size != m_OldSize)
		{
			m_ResizeWidth = size.Width;
			m_ResizeHeight = size.Height;
		}

		m_OldSize = size;
	}

	void OnVisibiltyChanged(IInspectable const&, VisibilityChangedEventArgs const& args)
	{
		m_windowVisible = args.Visible();
	}

	void OnWindowClosed(IInspectable const&, CoreWindowEventArgs const& args)
	{
		m_windowClosed = true;
	}

	void CreateRenderTarget()
	{
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			com_ptr<ID3D12Resource> pBackBuffer;
			m_pSwapChain->GetBuffer(i, __uuidof(pBackBuffer), pBackBuffer.put_void());
			m_pd3dDevice->CreateRenderTargetView(pBackBuffer.get(), nullptr, m_mainRenderTargetDescriptor[i]);
			m_mainRenderTargetResource[i] = pBackBuffer;
		}
	}

	void CleanupRenderTarget()
	{
		WaitForLastSubmittedFrame();

		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
			if (m_mainRenderTargetResource[i]) { m_mainRenderTargetResource[i] = nullptr; }
	}

	void WaitForLastSubmittedFrame()
	{
		FrameContext* frameCtx = &m_frameContext[m_frameIndex % NUM_FRAMES_IN_FLIGHT];

		UINT64 fenceValue = frameCtx->FenceValue;
		if (fenceValue == 0)
			return; // No fence was signaled

		frameCtx->FenceValue = 0;
		if (m_fence->GetCompletedValue() >= fenceValue)
			return;

		m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	FrameContext* WaitForNextFrameResources()
	{
		UINT nextFrameIndex = m_frameIndex + 1;
		m_frameIndex = nextFrameIndex;

		HANDLE waitableObjects[] = { m_hSwapChainWaitableObject, nullptr };
		DWORD numWaitableObjects = 1;

		FrameContext* frameCtx = &m_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
		UINT64 fenceValue = frameCtx->FenceValue;
		if (fenceValue != 0) // means no fence was signaled
		{
			frameCtx->FenceValue = 0;
			m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
			waitableObjects[1] = m_fenceEvent;
			numWaitableObjects = 2;
		}

		WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

		return frameCtx;
	}
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	CoreApplication::Run(make<App>());
}
