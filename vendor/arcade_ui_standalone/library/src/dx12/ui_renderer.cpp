// arcade_ui/src/dx12/ui_renderer.cpp
#include <arcade_ui/dx12/ui_renderer.hpp>
#include <d3dcompiler.h>

#include <cstring>
#include <algorithm>
#include <windows.h>
#include <d2d1helper.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace arcade_ui {

namespace {

constexpr uint32_t kAtlasDim = 1024;

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

const char* kUiShaderHlsl = R"(
cbuffer FrameConstants : register(b0) {
    float2 viewport_size;
    float2 padding;
};

struct InstanceData {
    float4 rect;         // x, y, width, height (px)
    float4 color;        // r, g, b, a (premultiplied)
    float4 border_color;
    float4 radii;        // tl, tr, br, bl
    float4 uv;           // u0, v0, u1, v1 (atlas)
    float border_width;
    float is_text;
    float2 pad;
};
StructuredBuffer<InstanceData> instances : register(t0);
StructuredBuffer<float> font_atlas : register(t1);

struct VSOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float4 border_color : COLOR1;
    float4 radii : EXTRA0;
    float2 local_pos : TEXCOORD0;
    float2 half_size : TEXCOORD1;
    float2 atlas_uv : TEXCOORD2;
    float border_width : EXTRA1;
    float is_text : EXTRA2;
};

VSOutput VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    VSOutput o;
    InstanceData inst = instances[instance_id];

    float2 quad[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0)
    };
    float2 uv = quad[vertex_id];
    float2 pixel_pos = inst.rect.xy + uv * inst.rect.zw;

    float2 ndc = (pixel_pos / viewport_size) * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.position = float4(ndc, 0.0, 1.0);

    o.color = inst.color;
    o.border_color = inst.border_color;
    o.radii = inst.radii;
    o.half_size = inst.rect.zw * 0.5;
    o.local_pos = (uv - 0.5) * inst.rect.zw;
    o.border_width = inst.border_width;
    o.atlas_uv = lerp(inst.uv.xy, inst.uv.zw, uv);
    o.is_text = inst.is_text;
    return o;
}

float RoundedBoxSDF(float2 p, float2 b, float4 r) {
    r.xy = (p.x > 0.0) ? r.yz : r.xw;
    float radius = (p.y > 0.0) ? r.x : r.y;
    float2 q = abs(p) - b + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float4 PSMain(VSOutput input) : SV_Target {
    if (input.is_text > 0.5) {
        int2 tc = int2(clamp(input.atlas_uv * 1024.0, 0.0, 1023.0));
        float coverage = font_atlas[tc.y * 1024 + tc.x];
        return input.color * coverage;
    }

    float dist = RoundedBoxSDF(input.local_pos, input.half_size, input.radii);
    float edge_softness = max(fwidth(dist), 0.0001);
    float alpha = saturate(-dist / edge_softness);

    float4 col = input.color * alpha;
    if (input.border_width > 0.0) {
        float inner_dist = dist + input.border_width;
        float border_mask = saturate(-dist / edge_softness) - saturate(-inner_dist / edge_softness);
        col = lerp(col, input.border_color * border_mask, border_mask);
    }
    return col;
}
)";

} // namespace

UiRenderer::~UiRenderer() = default;

Result<UiRenderer> UiRenderer::Create(ID3D12Device* device,
                                                  DXGI_FORMAT rtv_format,
                                                  uint32_t frames_in_flight) {
    if (!device || !frames_in_flight) return std::unexpected(Error{E_INVALIDARG,L"Device and frame count are required"});
    UiRenderer r;
    r.device_ = device;

    // 1. Compile UI Shaders
    auto compile=[](const char* entry,const char* target) -> Result<Microsoft::WRL::ComPtr<ID3DBlob>> {
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        const auto hr=D3DCompile(kUiShaderHlsl,std::strlen(kUiShaderHlsl),"arcade_ui",nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&errors);
        if (FAILED(hr)) return std::unexpected(Error{hr,L"Compile UI shader"});
        return blob;
    };
    auto vs_blob = compile("VSMain", "vs_5_0");
    if (!vs_blob) return std::unexpected(vs_blob.error());

    auto ps_blob = compile("PSMain", "ps_5_0");
    if (!ps_blob) return std::unexpected(ps_blob.error());

    // 2. Create Root Signature (Param 0: Constants b0, Param 1: SRV t0 instances, Param 2: SRV t1 font_atlas)
    D3D12_ROOT_PARAMETER1 root_params[3]{};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[0].Constants.ShaderRegister = 0;
    root_params[0].Constants.RegisterSpace = 0;
    root_params[0].Constants.Num32BitValues = 4;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[1].Descriptor.ShaderRegister = 0;
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[2].Descriptor.ShaderRegister = 1;
    root_params[2].Descriptor.RegisterSpace = 0;
    root_params[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC signature{};
    signature.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    signature.Desc_1_1.NumParameters=3; signature.Desc_1_1.pParameters=root_params;
    signature.Desc_1_1.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
    auto pipeline_hr=D3D12SerializeVersionedRootSignature(&signature,&serialized,&errors);
    if (FAILED(pipeline_hr)) return std::unexpected(Error{pipeline_hr,L"Serialize UI root signature"});
    pipeline_hr=device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&r.root_signature_));
    if (FAILED(pipeline_hr)) return std::unexpected(Error{pipeline_hr,L"Create UI root signature"});

    // 3. Create PSO with premultiplied alpha blend state
    D3D12_RENDER_TARGET_BLEND_DESC blend_desc{};
    blend_desc.BlendEnable = TRUE;
    blend_desc.LogicOpEnable = FALSE;
    blend_desc.SrcBlend = D3D12_BLEND_ONE;
    blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.pRootSignature=r.root_signature_.Get();
    pso_desc.VS={(*vs_blob)->GetBufferPointer(),(*vs_blob)->GetBufferSize()};
    pso_desc.PS={(*ps_blob)->GetBufferPointer(),(*ps_blob)->GetBufferSize()};
    pso_desc.BlendState.RenderTarget[0]=blend_desc;
    pso_desc.SampleMask=UINT_MAX;
    pso_desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso_desc.RasterizerState.DepthClipEnable=TRUE;
    pso_desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets=1; pso_desc.RTVFormats[0]=rtv_format; pso_desc.SampleDesc.Count=1;
    pipeline_hr=device->CreateGraphicsPipelineState(&pso_desc,IID_PPV_ARGS(&r.pso_));
    if (FAILED(pipeline_hr)) return std::unexpected(Error{pipeline_hr,L"Create UI pipeline"});

    // 4. DirectWrite shapes UTF-8 text; Direct2D rasterizes it with grayscale
    // antialiasing into a transparent CPU bitmap. Each frame gets its own GPU
    // upload atlas, matching the instance ring's lifetime.
    r.com_scope_ = std::make_unique<ComScope>();
    const HRESULT com_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE)
        return std::unexpected(Error::FromHresult(com_hr, L"CoInitializeEx"));
    r.com_scope_->initialized = SUCCEEDED(com_hr);

    if (HRESULT hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            r.d2d_factory_.GetAddressOf()); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"D2D1CreateFactory"));
    if (HRESULT hr = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(r.dwrite_factory_.GetAddressOf())); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"DWriteCreateFactory"));
    if (HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&r.wic_factory_)); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"WICImagingFactory"));
    if (HRESULT hr = r.wic_factory_->CreateBitmap(kAtlasDim, kAtlasDim,
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &r.text_bitmap_); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"CreateBitmap(TextAtlas)"));
    const auto target_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (HRESULT hr = r.d2d_factory_->CreateWicBitmapRenderTarget(r.text_bitmap_.Get(),
            target_props, &r.text_target_); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"CreateWicBitmapRenderTarget"));
    r.text_target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (HRESULT hr = r.text_target_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f),
            &r.text_brush_); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"CreateSolidColorBrush"));
    if (HRESULT hr = r.dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f, L"en-us", &r.text_format_); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"CreateTextFormat"));
    r.text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // 5. Allocate upload ring buffer for each frame slot
    r.frame_uploads_.resize(frames_in_flight);
    for (auto& fu : r.frame_uploads_) {
        fu.capacity = 4096; // 4096 quads initial capacity
        const UINT64 size_bytes = fu.capacity * sizeof(GpuUiInstance);

        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC res_desc{};
        res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        res_desc.Width = size_bytes;
        res_desc.Height = 1;
        res_desc.DepthOrArraySize = 1;
        res_desc.MipLevels = 1;
        res_desc.Format = DXGI_FORMAT_UNKNOWN;
        res_desc.SampleDesc = {1, 0};
        res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (HRESULT hr = device->CreateCommittedResource(
                &heap_props, D3D12_HEAP_FLAG_NONE, &res_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&fu.resource));
            FAILED(hr)) {
            return std::unexpected(Error::FromHresult(hr, L"CreateCommittedResource(UiInstanceUpload)"));
        }
        if (HRESULT map_hr=fu.resource->Map(0, nullptr, reinterpret_cast<void**>(&fu.mapped)); FAILED(map_hr))
            return std::unexpected(Error{map_hr,L"Map(UiInstanceUpload)"});

        D3D12_RESOURCE_DESC atlas_desc = res_desc;
        atlas_desc.Width = static_cast<UINT64>(kAtlasDim) * kAtlasDim * sizeof(float);
        if (HRESULT hr = device->CreateCommittedResource(
                &heap_props, D3D12_HEAP_FLAG_NONE, &atlas_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&fu.atlas_resource)); FAILED(hr)) {
            return std::unexpected(Error::FromHresult(hr, L"CreateCommittedResource(TextAtlas)"));
        }
        if (HRESULT hr = fu.atlas_resource->Map(0, nullptr, reinterpret_cast<void**>(&fu.atlas_mapped)); FAILED(hr)) {
            return std::unexpected(Error::FromHresult(hr, L"Map(TextAtlas)"));
        }
    }

    return r;
}

Result<void> UiRenderer::Render(ID3D12GraphicsCommandList* command_list,
                                             uint32_t frame_index,
                                             const FramePacket& packet) {
    if (!command_list || frame_uploads_.empty()) return std::unexpected(Error{E_INVALIDARG,L"Initialized renderer and command list are required"});
    if (packet.commands.empty()) return {};

    const uint32_t slot = frame_index % frame_uploads_.size();
    FrameUpload& fu = frame_uploads_[slot];

    uint32_t instance_count = 0;
    const float dpi = packet.dpi;
    uint32_t atlas_x = 1, atlas_y = 1, atlas_row_height = 0;
    text_target_->BeginDraw();
    text_target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    for (const auto& cmd : packet.commands) {
        if (instance_count >= fu.capacity) break;

        Rect px = DipToPx(cmd.bounds, dpi);

        if (cmd.kind == PrimitiveKind::GlyphRun) {
            if (cmd.text_str.empty()) continue;
            const auto wide = Utf8ToWide(cmd.text_str);
            if (wide.empty()) continue;
            const uint32_t width = static_cast<uint32_t>(std::ceil(px.width));
            const uint32_t height = static_cast<uint32_t>(std::ceil(px.height));
            if (width == 0 || height == 0 || width + 2 >= kAtlasDim || height + 2 >= kAtlasDim) continue;
            if (atlas_x + width + 1 >= kAtlasDim) {
                atlas_x = 1;
                atlas_y += atlas_row_height + 1;
                atlas_row_height = 0;
            }
            if (atlas_y + height + 1 >= kAtlasDim) continue;

            const float inset = cmd.text_left_aligned ? cmd.text_inset * dpi / 96.0f : 0.0f;
            const float top_inset = cmd.text_top_aligned ? 8.0f * dpi / 96.0f : 0.0f;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            const HRESULT layout_hr = dwrite_factory_->CreateTextLayout(wide.data(),
                static_cast<UINT32>(wide.size()), text_format_.Get(),
                (std::max)(1.0f, px.width - 2.0f * inset),
                (std::max)(1.0f, px.height - top_inset), &layout);
            if (FAILED(layout_hr)) continue;
            layout->SetTextAlignment(cmd.text_left_aligned ?
                DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_CENTER);
            layout->SetParagraphAlignment(cmd.text_top_aligned ?
                DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            layout->SetWordWrapping(cmd.text_top_aligned ? DWRITE_WORD_WRAPPING_WRAP :
                DWRITE_WORD_WRAPPING_NO_WRAP);
            layout->SetFontSize(14.0f * dpi / 96.0f,
                DWRITE_TEXT_RANGE{0, static_cast<UINT32>(wide.size())});

            text_target_->PushAxisAlignedClip(D2D1::RectF(
                static_cast<float>(atlas_x), static_cast<float>(atlas_y),
                static_cast<float>(atlas_x + width), static_cast<float>(atlas_y + height)),
                D2D1_ANTIALIAS_MODE_ALIASED);
            text_target_->DrawTextLayout(D2D1::Point2F(
                static_cast<float>(atlas_x) + inset - cmd.text_scroll_x * dpi / 96.0f,
                static_cast<float>(atlas_y) + top_inset - cmd.text_scroll_y * dpi / 96.0f),
                layout.Get(), text_brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            text_target_->PopAxisAlignedClip();

            GpuUiInstance& dst = fu.mapped[instance_count++];
            std::fill_n(reinterpret_cast<float*>(&dst), sizeof(dst) / sizeof(float), 0.0f);
            dst.rect[0] = px.x;
            dst.rect[1] = px.y;
            dst.rect[2] = px.width;
            dst.rect[3] = px.height;
            dst.color[0] = cmd.fill_color.r;
            dst.color[1] = cmd.fill_color.g;
            dst.color[2] = cmd.fill_color.b;
            dst.color[3] = cmd.fill_color.a;
            dst.uv[0] = static_cast<float>(atlas_x) / kAtlasDim;
            dst.uv[1] = static_cast<float>(atlas_y) / kAtlasDim;
            dst.uv[2] = static_cast<float>(atlas_x + width) / kAtlasDim;
            dst.uv[3] = static_cast<float>(atlas_y + height) / kAtlasDim;
            dst.is_text = 1.0f;
            atlas_x += width + 1;
            atlas_row_height = (std::max)(atlas_row_height, height);
        } else {
            // Box quad (Rect / RoundedRect)
            GpuUiInstance& dst = fu.mapped[instance_count++];
            dst.rect[0] = px.x;
            dst.rect[1] = px.y;
            dst.rect[2] = px.width;
            dst.rect[3] = px.height;

            dst.color[0] = cmd.fill_color.r;
            dst.color[1] = cmd.fill_color.g;
            dst.color[2] = cmd.fill_color.b;
            dst.color[3] = cmd.fill_color.a;

            dst.border_color[0] = cmd.border_color.r;
            dst.border_color[1] = cmd.border_color.g;
            dst.border_color[2] = cmd.border_color.b;
            dst.border_color[3] = cmd.border_color.a;

            dst.radii[0] = DipToPx(cmd.radii.top_left, dpi);
            dst.radii[1] = DipToPx(cmd.radii.top_right, dpi);
            dst.radii[2] = DipToPx(cmd.radii.bottom_right, dpi);
            dst.radii[3] = DipToPx(cmd.radii.bottom_left, dpi);

            dst.uv[0] = 0.0f;
            dst.uv[1] = 0.0f;
            dst.uv[2] = 0.0f;
            dst.uv[3] = 0.0f;

            dst.border_width = DipToPx(cmd.border_width, dpi);
            dst.is_text = 0.0f;
        }
    }

    if (HRESULT hr = text_target_->EndDraw(); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"TextAtlas EndDraw"));
    WICRect lock_rect{0, 0, static_cast<INT>(kAtlasDim), static_cast<INT>(kAtlasDim)};
    Microsoft::WRL::ComPtr<IWICBitmapLock> bitmap_lock;
    if (HRESULT hr = text_bitmap_->Lock(&lock_rect, WICBitmapLockRead, &bitmap_lock); FAILED(hr))
        return std::unexpected(Error::FromHresult(hr, L"TextAtlas Lock"));
    UINT stride = 0, data_size = 0;
    BYTE* pixels = nullptr;
    bitmap_lock->GetStride(&stride);
    bitmap_lock->GetDataPointer(&data_size, &pixels);
    for (uint32_t y = 0; y < kAtlasDim; ++y) {
        const BYTE* row = pixels + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < kAtlasDim; ++x)
            fu.atlas_mapped[static_cast<size_t>(y) * kAtlasDim + x] = row[x * 4 + 3] / 255.0f;
    }

    if (instance_count == 0) return {};

    // Bind Pipeline & Root Signature
    command_list->SetPipelineState(pso_.Get());
    command_list->SetGraphicsRootSignature(root_signature_.Get());

    // Root Constants: Viewport size in physical px
    const Vec2 viewport_px = DipToPx(packet.viewport_size, dpi);
    const float constants[4] = {viewport_px.x, viewport_px.y, 0.0f, 0.0f};
    command_list->SetGraphicsRoot32BitConstants(0, 4, constants, 0);

    // Root Descriptor: Structured Buffer SRV (instances)
    command_list->SetGraphicsRootShaderResourceView(1, fu.resource->GetGPUVirtualAddress());

    // Root Descriptor: Structured Buffer SRV (font atlas)
    command_list->SetGraphicsRootShaderResourceView(2, fu.atlas_resource->GetGPUVirtualAddress());

    // Draw all quads in one instanced draw call
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(6, instance_count, 0, 0);

    return {};
}

} // namespace arcade_ui
