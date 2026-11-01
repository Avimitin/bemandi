#pragma once
namespace bemandi {
struct Config;
bool start_video(const Config& config);
void stop_video();
bool video_running();
}
