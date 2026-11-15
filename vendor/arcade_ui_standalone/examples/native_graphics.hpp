#pragma once
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
namespace arcade_ui::examples {
using Microsoft::WRL::ComPtr;
inline void Check(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("DX12 error " + std::to_string(hr)); }
struct Graphics {
    ComPtr<ID3D12Device> device; ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap; ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence; std::array<ComPtr<ID3D12Resource>,2> buffers;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); UINT64 value=0;
    UINT increment=0,width=0,height=0;
    ~Graphics() { if (queue && fence && event) { try { Wait(); } catch (...) {} } if (event) CloseHandle(event); }
    void Wait() {
        Check(queue->Signal(fence.Get(),++value)); Check(fence->SetEventOnCompletion(value,event));
        if (WaitForSingleObject(event,INFINITE)!=WAIT_OBJECT_0) throw std::runtime_error("GPU wait failed");
        Check(device->GetDeviceRemovedReason());
    }
    void Targets() {
        auto handle=heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i=0;i<2;++i) { Check(swap->GetBuffer(i,IID_PPV_ARGS(&buffers[i]))); device->CreateRenderTargetView(buffers[i].Get(),nullptr,handle); handle.ptr+=increment; }
    }
    void Create(HWND window,UINT w,UINT h,bool warp) {
        if (!event) throw std::runtime_error("Fence event creation failed");
        ComPtr<ID3D12Debug> debug; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter; if (warp) Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC queue_desc{}; Check(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)));
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=w; desc.Height=h; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initial; Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&initial)); Check(initial.As(&swap));
        factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{}; heap_desc.NumDescriptors=2; heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&heap))); increment=device->GetDescriptorHandleIncrementSize(heap_desc.Type);
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))); Check(list->Close());
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); width=w; height=h; Targets();
    }
    void Resize(UINT w,UINT h) {
        if (w==width && h==height) return;
        Wait(); for (auto& buffer:buffers) buffer.Reset();
        Check(swap->ResizeBuffers(2,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,0)); width=w; height=h; Targets();
    }
    D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to}; return b;
    }
    void Screenshot(UINT index,const std::string& path) {
        Wait(); Check(allocator->Reset()); Check(list->Reset(allocator.Get(),nullptr));
        auto texture=buffers[index]->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes=0;
        device->GetCopyableFootprints(&texture,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=bytes; desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES properties{}; properties.Type=D3D12_HEAP_TYPE_READBACK; ComPtr<ID3D12Resource> readback;
        Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
        auto barrier=Barrier(buffers[index].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE); list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=readback.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=buffers[index].Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); list->ResourceBarrier(1,&barrier);
        Check(list->Close()); ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists); Wait();
        std::byte* data=nullptr; D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)}; Check(readback->Map(0,&range,reinterpret_cast<void**>(&data)));
        BITMAPFILEHEADER file{}; file.bfType=0x4D42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+width*height*4;
        BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=width; info.biHeight=-static_cast<LONG>(height); info.biPlanes=1; info.biBitCount=32;
        std::ofstream output(path,std::ios::binary); output.write(reinterpret_cast<char*>(&file),sizeof(file)); output.write(reinterpret_cast<char*>(&info),sizeof(info));
        std::vector<std::byte> row(width*4);
        for (UINT y=0;y<height;++y) { std::memcpy(row.data(),data+footprint.Offset+y*footprint.Footprint.RowPitch,row.size()); for (UINT x=0;x<width;++x) std::swap(row[x*4],row[x*4+2]); output.write(reinterpret_cast<char*>(row.data()),row.size()); }
        D3D12_RANGE written{0,0}; readback->Unmap(0,&written); if (!output) throw std::runtime_error("Screenshot write failed");
    }
};


}
