#pragma once
#include <d3d9.h>
// Windows COM ABI indices, independent of game builds.
namespace bemandi::d3d9_abi {
using Create9 = IDirect3D9*(WINAPI*)(UINT);
using Create9Ex = HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);
using CreateDevice = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using CreateDeviceEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex**);
using Present = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using PresentEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using SwapPresent = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using ResetEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
inline constexpr unsigned create_device = 16;
inline constexpr unsigned create_device_ex = 20;
inline constexpr unsigned present = 17;
inline constexpr unsigned present_ex = 121;
inline constexpr unsigned swap_present = 3;
inline constexpr unsigned reset = 16;
inline constexpr unsigned reset_ex = 132;
}
