#pragma once
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <arcade_ui/core/host.hpp>
namespace arcade_ui {
// Adapter for a normal Win32 WndProc. Window ownership stays with the application.
class Win32MessageAdapter {
    wchar_t high_surrogate_=0;
    HWND caret_window_=nullptr;
    static std::wstring Composition(HIMC context,DWORD kind) {
        const LONG bytes=ImmGetCompositionStringW(context,kind,nullptr,0);
        if (bytes<=0 || bytes>1024*1024 || bytes%sizeof(wchar_t)) return {};
        std::wstring result(static_cast<size_t>(bytes)/sizeof(wchar_t),L'\0');
        ImmGetCompositionStringW(context,kind,result.data(),bytes); return result;
    }
public:
    Win32MessageAdapter() = default;
    Win32MessageAdapter(const Win32MessageAdapter&) = delete;
    Win32MessageAdapter& operator=(const Win32MessageAdapter&) = delete;
    ~Win32MessageAdapter() { if (caret_window_) DestroyCaret(); }
    bool ProcessMessage(UiHost& host,HWND window,UINT message,WPARAM wparam,LPARAM lparam,float dpi=96) {
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0, shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        switch (message) {
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: {
            const auto kind=message==WM_MOUSEMOVE ? PointerEvent::Kind::Move : message==WM_LBUTTONDOWN ? PointerEvent::Kind::Down : PointerEvent::Kind::Up;
            if (message==WM_LBUTTONDOWN) { SetFocus(window); SetCapture(window); }
            if (message==WM_LBUTTONUP) ReleaseCapture();
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track);
            return host.DispatchPointer({.kind=kind,.position_dip=PxToDip(Vec2{static_cast<float>(GET_X_LPARAM(lparam)),static_cast<float>(GET_Y_LPARAM(lparam))},dpi)});
        }
        case WM_MOUSELEAVE: return host.DispatchPointer({.kind=PointerEvent::Kind::Leave});
        case WM_MOUSEWHEEL: return host.DispatchPointer({.kind=PointerEvent::Kind::Wheel,.wheel_delta=static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam))});
        case WM_KEYDOWN: return host.DispatchKey(static_cast<uint32_t>(wparam),ctrl,shift);
        case WM_CHAR: {
            char32_t cp=static_cast<char32_t>(wparam);
            if (ctrl && cp<32) return true;
            if (cp>=0xD800 && cp<=0xDBFF) { high_surrogate_=static_cast<wchar_t>(cp); return true; }
            if (cp>=0xDC00 && cp<=0xDFFF && high_surrogate_) cp=0x10000+((high_surrogate_-0xD800)<<10)+(cp-0xDC00);
            high_surrogate_=0; return host.DispatchText(cp);
        }
        case WM_KILLFOCUS:
            high_surrogate_=0; host.CancelTextComposition(); host.DispatchComposition(CompositionPhase::End); return false;
        case WM_IME_STARTCOMPOSITION: return host.DispatchComposition(CompositionPhase::Start);
        case WM_IME_ENDCOMPOSITION: return host.DispatchComposition(CompositionPhase::End);
        case WM_IME_COMPOSITION: {
            HIMC context=ImmGetContext(window); if (!context) return false;
            if (lparam&GCS_RESULTSTR) host.DispatchComposition(CompositionPhase::Commit,Composition(context,GCS_RESULTSTR));
            if (lparam&GCS_COMPSTR) host.DispatchComposition(CompositionPhase::Update,Composition(context,GCS_COMPSTR),static_cast<size_t>((std::max)(0L,ImmGetCompositionStringW(context,GCS_CURSORPOS,nullptr,0))));
            ImmReleaseContext(window,context); return true;
        }
        }
        return false;
    }
    void UpdateImePosition(UiHost& host,HWND window,float dpi=96) {
        HIMC context=ImmGetContext(window);
        if (host.TakeImeCancelRequest() && context) ImmNotifyIME(context,NI_COMPOSITIONSTR,CPS_CANCEL,0);
        const auto caret=host.GetTextInputCaret();
        if (!caret) { if (caret_window_) DestroyCaret(); caret_window_=nullptr; }
        else {
            const auto pixel=DipToPx(caret->bounds,dpi);
            if (caret_window_) DestroyCaret();
            if (CreateCaret(window,nullptr,1,(std::max)(1,static_cast<int>(pixel.height)))) caret_window_=window;
            SetCaretPos(static_cast<int>(pixel.x),static_cast<int>(pixel.y));
            if (context) {
                COMPOSITIONFORM composition{CFS_POINT,{static_cast<LONG>(pixel.x),static_cast<LONG>(pixel.y)},{}};
                ImmSetCompositionWindow(context,&composition);
                CANDIDATEFORM candidate{}; candidate.dwStyle=CFS_EXCLUDE;
                candidate.ptCurrentPos={static_cast<LONG>(pixel.x),static_cast<LONG>(pixel.Bottom())};
                candidate.rcArea={static_cast<LONG>(pixel.x),static_cast<LONG>(pixel.y),static_cast<LONG>(pixel.Right()),static_cast<LONG>(pixel.Bottom())};
                ImmSetCandidateWindow(context,&candidate);
            }
        }
        if (context) ImmReleaseContext(window,context);
    }
};
}
