#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <mmsystem.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "bemandi_api.h"
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr,const char* op) {
    if(FAILED(hr)) { char msg[128]; sprintf_s(msg,"%s failed 0x%08lX",op,static_cast<unsigned long>(hr)); throw std::runtime_error(msg); }
}
int wmain(int argc,wchar_t** argv) {
    BemandiStop stop=nullptr;
    HWND window=nullptr;
    try {
        std::wstring dll=L"bemandi.dll",ini;
        unsigned seconds=12,fps=120;
        bool reset=false,legacy=false,swap=false,baseline=false;
        for(int i=1;i<argc;++i) {
            std::wstring a=argv[i];
            if(a==L"--dll" && i+1<argc) dll=argv[++i];
            else if(a==L"--ini" && i+1<argc) ini=argv[++i];
            else if(a==L"--seconds" && i+1<argc) seconds=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(a==L"--fps" && i+1<argc) fps=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(a==L"--reset") reset=true;
            else if(a==L"--legacy") legacy=true;
            else if(a==L"--swap") swap=true;
            else if(a==L"--baseline") baseline=true;
            else throw std::runtime_error("Unknown/missing argument");
        }
        if(fps<1 || fps>240 || seconds<1 || seconds>300) throw std::runtime_error("Invalid duration/fps");
        BemandiGetStats stats=nullptr;
        if(!baseline) {
            auto module=LoadLibraryW(std::filesystem::absolute(dll).c_str());
            if(!module) throw std::runtime_error("LoadLibrary failed");
            auto start=reinterpret_cast<BemandiStart>(GetProcAddress(module,"bemandi_start"));
            stop=reinterpret_cast<BemandiStop>(GetProcAddress(module,"bemandi_stop"));
            stats=reinterpret_cast<BemandiGetStats>(GetProcAddress(module,"bemandi_get_stats"));
            if(!start || !stop || !stats || !start(ini.empty()?nullptr:ini.c_str())) throw std::runtime_error("Plugin initialization failed");
        }
        WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"BemandiCaptureTest";
        RegisterClassW(&wc);
        window=CreateWindowW(wc.lpszClassName,L"Bemandi synthetic D3D9 capture test",WS_OVERLAPPEDWINDOW,
            0,0,640,360,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window) throw std::runtime_error("CreateWindow failed");
        ComPtr<IDirect3D9> d3d;
        ComPtr<IDirect3D9Ex> d3dex;
        ComPtr<IDirect3DDevice9> device;
        ComPtr<IDirect3DDevice9Ex> deviceex;
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth=1920; pp.BackBufferHeight=1080; pp.BackBufferFormat=D3DFMT_A8R8G8B8;
        pp.BackBufferCount=1; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=window;
        pp.Windowed=TRUE; pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        DWORD flags=D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
        if(legacy) {
            d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
            if(!d3d) throw std::runtime_error("Direct3DCreate9 failed");
            check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,flags,&pp,&device),"CreateDevice");
        } else {
            check(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3dex),"Direct3DCreate9Ex");
            check(d3dex->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,flags,&pp,nullptr,&deviceex),"CreateDeviceEx");
            check(deviceex.As(&device),"QueryInterface");
        }
        timeBeginPeriod(1);
        using Clock=std::chrono::steady_clock;
        FILETIME created{},exited{},kernel0{},user0{},kernel1{},user1{};
        GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel0,&user0);
        const auto beginning=Clock::now();
        auto deadline=beginning;
        const auto period=std::chrono::nanoseconds(1000000000/fps);
        std::vector<double> present_times;
        bool reset_done=false;
        unsigned frames=0,state_errors=0;
        const D3DCOLOR colors[]{D3DCOLOR_XRGB(255,0,0),D3DCOLOR_XRGB(0,255,0),D3DCOLOR_XRGB(0,0,255),D3DCOLOR_XRGB(255,255,255)};
        while(Clock::now()-beginning<std::chrono::seconds(seconds)) {
            MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if(reset && !reset_done && Clock::now()-beginning>std::chrono::seconds(seconds/2)) {
                check(deviceex ? deviceex->ResetEx(&pp,nullptr) : device->Reset(&pp),"Reset");
                reset_done=true;
            }
            check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"Clear");
            for(unsigned band=0;band<4;++band) {
                D3DRECT rect{static_cast<LONG>(band*480),0,static_cast<LONG>((band+1)*480),1080};
                check(device->Clear(1,&rect,D3DCLEAR_TARGET,colors[band],1,0),"Clear band");
            }
            D3DRECT moving{static_cast<LONG>(frames*8%1800),900,static_cast<LONG>(frames*8%1800+100),1000};
            check(device->Clear(1,&moving,D3DCLEAR_TARGET,0xff000000,1,0),"Clear moving rectangle");
            // Deliberately unusual game state must survive the conversion pass.
            check(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"Set state");
            check(device->SetRenderState(D3DRS_COLORWRITEENABLE,3),"Set state");
            const auto before=Clock::now();
            if(swap) { ComPtr<IDirect3DSwapChain9> s; check(device->GetSwapChain(0,&s),"GetSwapChain"); check(s->Present(nullptr,nullptr,nullptr,nullptr,0),"Swap Present"); }
            else check(deviceex ? deviceex->PresentEx(nullptr,nullptr,nullptr,nullptr,0) : device->Present(nullptr,nullptr,nullptr,nullptr),"Present");
            present_times.push_back(std::chrono::duration<double,std::micro>(Clock::now()-before).count());
            DWORD scissor=0,write=0;
            device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor); device->GetRenderState(D3DRS_COLORWRITEENABLE,&write);
            if(scissor!=TRUE || write!=3) ++state_errors;
            device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE); device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
            ++frames;
            deadline+=period;
            if(deadline<Clock::now()-period) deadline=Clock::now();
            std::this_thread::sleep_until(deadline);
        }
        const double elapsed=std::chrono::duration<double>(Clock::now()-beginning).count();
        GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel1,&user1);
        auto cpu_ticks=[](FILETIME t){return (static_cast<uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
        const double cpu_seconds=static_cast<double>(cpu_ticks(kernel1)+cpu_ticks(user1)-cpu_ticks(kernel0)-cpu_ticks(user0))/10000000.0;
        timeEndPeriod(1);
        BemandiStats s{}; s.size=sizeof(s);
        if(stop) stop();
        if(stats) stats(&s);
        std::sort(present_times.begin(),present_times.end());
        auto percentile=[&](double p){return present_times[static_cast<size_t>((present_times.size()-1)*p)];};
        printf("{\"frames\":%u,\"captured\":%llu,\"sent\":%llu,\"busy\":%llu,\"no_receiver\":%llu,\"errors\":%llu,\"resets\":%llu,\"max_capture_us\":%llu,\"present_p50_us\":%.1f,\"present_p99_us\":%.1f,\"state_errors\":%u,\"elapsed_seconds\":%.3f,\"cpu_core_percent\":%.2f}\n",
            frames,s.captured,s.completed,s.dropped_busy,s.skipped_no_receiver,s.errors,s.resets,s.max_capture_us,percentile(.5),percentile(.99),state_errors,elapsed,100*cpu_seconds/elapsed);
        device.Reset(); deviceex.Reset(); d3d.Reset(); d3dex.Reset(); DestroyWindow(window);
        return (s.errors || state_errors) ? 2 : 0;
    } catch(const std::exception& e) {
        if(stop) stop();
        if(window) DestroyWindow(window);
        fprintf(stderr,"%s\n",e.what()); return 1;
    }
}
