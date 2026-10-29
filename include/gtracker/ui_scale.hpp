#pragma once
#include <imgui.h>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace gtracker::ui {
inline constexpr std::array<float,5> scales{1.0f,1.5f,2.0f,3.0f,4.0f};
inline float requested_scale=1;
inline int menu_frame=-1;
inline ImVec2 content_position(){const auto* view=ImGui::GetMainViewport();return ImVec2(view->Pos.x,view->Pos.y+(menu_frame==ImGui::GetFrameCount()?ImGui::GetFrameHeight():0));}
inline ImVec2 content_size(){const auto* view=ImGui::GetMainViewport();return ImVec2(view->Size.x,view->Size.y-(menu_frame==ImGui::GetFrameCount()?ImGui::GetFrameHeight():0));}
inline float pixels(float value){return value*ImGui::GetFontSize()/16.0f;}
inline int scale_index(float value){for(std::size_t i=0;i<scales.size();++i)if(std::abs(scales[i]-value)<0.01f)return static_cast<int>(i);throw std::invalid_argument("UI scale must be 1, 1.5, 2, 3 or 4");}
inline void scale_menu(){
    if(ImGui::BeginMainMenuBar()){
        menu_frame=ImGui::GetFrameCount();
        const auto label="UI scale: "+std::to_string(static_cast<int>(requested_scale*100))+"%";
        if(ImGui::BeginMenu(label.c_str())){
            for(float scale:scales){const auto text=std::to_string(static_cast<int>(scale*100))+"%";if(ImGui::MenuItem(text.c_str(),nullptr,requested_scale==scale))requested_scale=scale;}
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}
}
