#pragma once
#include <imgui.h>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace gtracker::ui {
inline constexpr std::array<float,5> scales{1.0f,1.5f,2.0f,3.0f,4.0f};
inline float requested_scale=1;
inline ImVec2 content_position(){return ImGui::GetMainViewport()->Pos;}
inline ImVec2 content_size(){return ImGui::GetMainViewport()->Size;}
inline float pixels(float value){return value*ImGui::GetFontSize()/16.0f;}
inline int scale_index(float value){for(std::size_t i=0;i<scales.size();++i)if(std::abs(scales[i]-value)<0.01f)return static_cast<int>(i);throw std::invalid_argument("UI scale must be 1, 1.5, 2, 3 or 4");}
inline std::string scale_label(){return "UI scale: "+std::to_string(static_cast<int>(requested_scale*100))+"%";}
inline void scale_button(){
    if(ImGui::Button(scale_label().c_str()))ImGui::OpenPopup("UI scale choices");
    if(ImGui::BeginPopup("UI scale choices")){
        for(float scale:scales){const auto text=std::to_string(static_cast<int>(scale*100))+"%";if(ImGui::MenuItem(text.c_str(),nullptr,requested_scale==scale))requested_scale=scale;}
        ImGui::EndPopup();
    }
}
}
