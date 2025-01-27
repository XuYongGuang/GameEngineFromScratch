#pragma once
#include "BlendState.hpp"
#include "RasterizerState.hpp"
#include "DepthStencilState.hpp"
#include "TopologyType.hpp"

namespace My {
    struct PipelineState {
        BlendState	blend_state;

        RasterizerState	rasterizer_state;

        DepthStencilState	depth_stencil_state;

        TopologyType::Enum	topology_type;


        void reflectMembers() {
            ImGui::Text("blend_state");
            blend_state.reflectMembers();

            ImGui::Text("rasterizer_state");
            rasterizer_state.reflectMembers();

            ImGui::Text("depth_stencil_state");
            depth_stencil_state.reflectMembers();

            ImGui::Combo( "topology_type", (int32_t*)&topology_type, TopologyType::s_value_names, TopologyType::Count );

        }

        void reflectUI() {
            ImGui::Begin("PipelineState");
            reflectMembers();
            ImGui::End();
        }
    };
} // namespace My
