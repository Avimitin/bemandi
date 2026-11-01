#include "ndi.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    try {
        const std::string wanted=argc>1?argv[1]:"IIDX Main";
        const int seconds=argc>2?std::stoi(argv[2]):15;
        const int slow_ms=argc>3?std::stoi(argv[3]):0;
        bemandi::NdiRuntime runtime;
        auto api=runtime.api;
        NDIlib_find_create_t find_desc{}; find_desc.show_local_sources=true;
        auto finder=api->find_create_v2(&find_desc);
        if(!finder) throw std::runtime_error("NDI finder creation failed");
        NDIlib_recv_instance_t receiver=nullptr;
        using Clock=std::chrono::steady_clock;
        const auto end=Clock::now()+std::chrono::seconds(seconds);
        unsigned frames=0,bad_colors=0,bad_format=0,nonmonotonic=0,motion_changes=0;
        int previous_marker=-1;
        int64_t previous=0;
        while(Clock::now()<end) {
            if(!receiver) {
                api->find_wait_for_sources(finder,100);
                uint32_t count=0; const auto sources=api->find_get_current_sources(finder,&count);
                for(uint32_t i=0;i<count;++i) {
                    if(std::string(sources[i].p_ndi_name).find(wanted)==std::string::npos) continue;
                    NDIlib_recv_create_v3_t desc{};
                    desc.source_to_connect_to=sources[i];
                    desc.color_format=NDIlib_recv_color_format_BGRX_BGRA;
                    desc.bandwidth=NDIlib_recv_bandwidth_highest;
                    desc.allow_video_fields=false;
                    receiver=api->recv_create_v3(&desc);
                    break;
                }
                continue;
            }
            NDIlib_video_frame_v2_t v{};
            const auto type=api->recv_capture_v2(receiver,&v,nullptr,nullptr,100);
            if(type!=NDIlib_frame_type_video) continue;
            ++frames;
            if(v.xres!=1920 || v.yres!=1080 || v.frame_rate_N!=120 || v.frame_rate_D!=1 ||
                (v.FourCC!=NDIlib_FourCC_type_BGRA && v.FourCC!=NDIlib_FourCC_type_BGRX)) ++bad_format;
            else {
                const unsigned expected[4][3]{{0,0,255},{0,255,0},{255,0,0},{255,255,255}};
                for(int band=0;band<4;++band) {
                    const auto p=v.p_data+100*v.line_stride_in_bytes+(band*480+240)*4;
                    for(int channel=0;channel<3;++channel)
                        if(std::abs(static_cast<int>(p[channel])-static_cast<int>(expected[band][channel]))>25) ++bad_colors;
                }
                int marker=-1;
                for(int x=0;x<v.xres;x+=8) {
                    const auto p=v.p_data+950*v.line_stride_in_bytes+x*4;
                    if(p[0]<25 && p[1]<25 && p[2]<25) { marker=x; break; }
                }
                if(marker>=0 && previous_marker>=0 && marker!=previous_marker) ++motion_changes;
                previous_marker=marker;
            }
            if(previous && v.timecode<=previous) ++nonmonotonic;
            previous=v.timecode;
            api->recv_free_video_v2(receiver,&v);
            if(slow_ms>0) Sleep(static_cast<DWORD>(slow_ms));
        }
        if(receiver) api->recv_destroy(receiver);
        api->find_destroy(finder);
        printf("{\"received\":%u,\"bad_colors\":%u,\"bad_format\":%u,\"nonmonotonic\":%u,\"motion_changes\":%u}\n",frames,bad_colors,bad_format,nonmonotonic,motion_changes);
        return frames>=20 && motion_changes>=5 && !bad_colors && !bad_format && !nonmonotonic ? 0 : 2;
    } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what()); return 1; }
}
