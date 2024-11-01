# ImGui-UWP
ImGui-UWP is a UWP backend for [Dear ImGui](https://github.com/ocornut/imgui).

ImGui-UWP has been tested against the [DirectX 11](https://github.com/ahmed605/imgui-uwp/tree/master/examples/example_uwp_directx11) and [DirectX 12](https://github.com/ahmed605/imgui-uwp/tree/master/examples/example_uwp_directx12) rendering backends but it should work fine with other rendering backends that are compatible with UWP too (e.g. SDL2).

The [examples folder](https://github.com/ahmed605/imgui-uwp/tree/master/examples) contains C++/WinRT UWP CoreApplication examples for using ImGui-UWP with both DirectX 11 and DirectX 12.

### Supported features

- [x] Clipboard support
- [x] Mouse support (can discriminate Mouse/TouchScreen/Pen)
- [x] Keyboard support
- [x] Gamepad support
- [x] Mouse cursor shape and visibility
- [x] DPI Scaling (see [this code](https://github.com/ahmed605/imgui-uwp/blob/eff8fc121213971a704a5d3412fdba2148b4d82d/examples/example_uwp_directx11/main.cpp#L70-L73) for an example of how to use this)
- [ ] IME 

### Screenshot

![DirectX 11](https://github.com/ahmed605/imgui-uwp/assets/34550324/d95d4604-93d4-44fa-89fa-5a56dc5cf1da)

![DirectX 12](https://github.com/ahmed605/imgui-uwp/assets/34550324/85a198eb-f8a0-4298-9f3e-8631ba080c98)

![XAML SwapChainPanel - DirectX 12](https://github.com/user-attachments/assets/a5786a32-3da8-4bcd-862c-3dfc25b86bac)

![GameBar Widget - DirectX 12](https://github.com/user-attachments/assets/085d1c73-c0f0-4f3f-8174-eec6cb2d8cce)


