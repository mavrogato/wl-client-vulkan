#ifndef INCLUDE_BUILTIN_RESOURCES_HPP_B31AF543_0FA3_40EE_B197_47B924169949
#define INCLUDE_BUILTIN_RESOURCES_HPP_B31AF543_0FA3_40EE_B197_47B924169949

#include <span>
#include <cstdint>
#include "versor.hpp"

namespace builtin
{
    struct mesh {
        struct vertex {
            aux::vec3f position;
            aux::vec3f normal;
        };
        std::span<vertex const> vertices;
        std::span<aux::vec3i const> indices;
    };

    extern mesh const meshes[3]; // constexpr

    extern uint32_t const vshader_binary[791];
    extern uint32_t const fshader_binary[141];
}

#endif/*INCLUDE_BUILTIN_RESOURCES_HPP_B31AF543_0FA3_40EE_B197_47B924169949*/
