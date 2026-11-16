// arcade_ui/dx12/ui_renderer.hpp
//
// DirectX 12 native UI pass with instanced quad rendering and premultiplied alpha (§13).
#pragma once

#include <d3d12.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <span>
#include <vector>
#include <memory>
#include <arcade_ui/error.hpp>
#include <arcade_ui/core/host.hpp>

namespace arcade_ui {

struct alignas(16) GpuUiInstance {
    float rect[4];         // x, y, width, height (physical px)
    float color[4];        // r, g, b, a (premultiplied)
    float border_color[4]; // r, g, b, a
    float radii[4];        // top_left, top_right, bottom_right, bottom_left
    float uv[4];           // u0, v0, u1, v1 (atlas coordinates)
    float border_width;
    float is_text;         // 0.0f = SDF rounded box, 1.0f = text glyph from atlas
    float pad[2];
};

class UiRenderer {
public:
    static Result<UiRenderer> Create(ID3D12Device* device,
                                                  DXGI_FORMAT rtv_format,
                                                  uint32_t frames_in_flight = 2);

    UiRenderer() = default;
    ~UiRenderer();
    UiRenderer(UiRenderer&&) noexcept = default;
    UiRenderer& operator=(UiRenderer&&) noexcept = default;

    // Render an arcade_ui FramePacket into the active command list (§13.1, §13.2)
    Result<void> Render(ID3D12GraphicsCommandList* command_list,
                                     uint32_t frame_index,
                                     const FramePacket& packet);

private:
    struct ComScope {
        bool initialized = false;
        ~ComScope() { if (initialized) ::CoUninitialize(); }
    };
    std::unique_ptr<ComScope> com_scope_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> text_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_factory_;
    Microsoft::WRL::ComPtr<IWICBitmap> text_bitmap_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;

    struct FrameUpload {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        GpuUiInstance* mapped = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> atlas_resource;
        float* atlas_mapped = nullptr;
        uint32_t capacity = 0;
    };
    std::vector<FrameUpload> frame_uploads_;
};

} // namespace arcade_ui
