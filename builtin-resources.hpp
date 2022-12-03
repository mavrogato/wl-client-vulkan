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

    extern mesh const meshes[3];
    inline mesh const& pyramid     = meshes[0];
    inline mesh const& icosahedron = meshes[1];
    inline mesh const& teapot      = meshes[2];

    extern uint32_t const vshader_binary[791];
    extern uint32_t const fshader_binary[141];
}

#endif/*INCLUDE_BUILTIN_RESOURCES_HPP_B31AF543_0FA3_40EE_B197_47B924169949*/
