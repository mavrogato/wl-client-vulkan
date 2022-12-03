
#include <concepts>
#include <iostream>
#include <memory>
#include <tuple>
#include <utility>
#include <bitset>
#include <limits>
#include <coroutine>
#include <initializer_list>
#include <thread>
#include <complex>
//#include <source_location>
//#include <ranges>

#include <cstdint>

#include <CL/sycl.hpp>

#include <wayland-client.h>
#include "xdg-shell-v6-client.h"
#include "zwp-tablet-v2-client.h"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.hpp>

#include "versor.hpp"
#include "builtin-resources.hpp"

namespace aux
{
    namespace oneapi_clang14_rooting
    {
        struct source_location {
            unsigned int line_ = 0;
            unsigned int col_ = 0;
            const char* file_ = nullptr;
            const char* func_ = nullptr;

            static constexpr source_location current(
                const char* file = __builtin_FILE(),
                const char* func = __builtin_FUNCTION(),
                unsigned int line = __builtin_LINE(),
                unsigned int col = __builtin_COLUMN()) noexcept {
                return source_location {
                    .line_ = line,
                    .col_ = col,
                    .file_ = file,
                    .func_ = func,
                };
            }
            constexpr unsigned int line() const noexcept { return this->line_; }
            constexpr unsigned int column() const noexcept { return this->col_; }
            constexpr const char *file_name() const noexcept { return this->file_; }
            constexpr const char *function_name() const noexcept { return this->func_; }
        };
    } // ::oneapi_clang14_rooting

    using slocation = oneapi_clang14_rooting::source_location;

    template <class Ch, class... Args>
    auto& operator<<(std::basic_ostream<Ch>& output, std::tuple<Args...> const& tuple) noexcept {
        output.put('(');
        [&]<size_t... I>(std::index_sequence<I...>) noexcept {
            (void) std::initializer_list<int>{(output << (I == 0 ? "" : " ") << std::get<I>(tuple), 0)... };
        } (std::index_sequence_for<Args...>());
        output.put(')');
        return output;
    }

    template <class T, class D>
    auto safe_ptr(T* ptr, D del, slocation loc = slocation::current()) {
        if (ptr == nullptr) {
            throw loc;
        }
        return std::unique_ptr<T, D>(ptr, del);
    }

    inline auto lamed() noexcept {
        return [](auto...) { };
    }
    inline auto lamed(auto&& closure) noexcept {
        static auto cache = closure;
        return [](auto... args) {
            return cache(args...);
        };
    }

    template <class First, class... Rest>
    struct chunk : std::array<First, 1 + sizeof... (Rest)> {
        using array_type = std::array<First, 1 + sizeof... (Rest)>;
        constexpr chunk(First first, Rest... rest) noexcept
            : array_type{ std::forward<First>(first), std::forward<Rest>(rest)... }
            {
            }
        constexpr operator First const*() const noexcept { return this->data(); }
    };

} // ::aux

inline namespace wayland_client_support
{
    inline auto safe_ptr(wl_display* ptr, aux::slocation loc = aux::slocation::current()) {
        return aux::safe_ptr(ptr, wl_display_disconnect, loc);
    }
#define INTERN_SAFE_PTR(WL_CLIENT)                                      \
    inline auto safe_ptr(WL_CLIENT* ptr, aux::slocation loc = aux::slocation::current()) { \
        return aux::safe_ptr(ptr, WL_CLIENT##_destroy, loc);            \
    }
    INTERN_SAFE_PTR(wl_registry)
    INTERN_SAFE_PTR(wl_compositor)
    INTERN_SAFE_PTR(wl_seat)
    INTERN_SAFE_PTR(wl_surface)
    INTERN_SAFE_PTR(wl_keyboard)
    INTERN_SAFE_PTR(wl_pointer)
    INTERN_SAFE_PTR(wl_touch)
    INTERN_SAFE_PTR(zxdg_shell_v6)
    INTERN_SAFE_PTR(zxdg_surface_v6)
    INTERN_SAFE_PTR(zwp_tablet_manager_v2)
#undef INTERN_SAFE_PTR

    template <class WL_CLIENT> struct wl_listener { using type = void; };
#define INTERN_LISTENER(WL_CLIENT)                                      \
    template <> struct wl_listener<WL_CLIENT> { using type = WL_CLIENT##_listener; };
    INTERN_LISTENER(wl_registry)
    INTERN_LISTENER(wl_seat)
    INTERN_LISTENER(zxdg_shell_v6)
    INTERN_LISTENER(zxdg_surface_v6)
    INTERN_LISTENER(wl_pointer)
    INTERN_LISTENER(wl_keyboard)
    INTERN_LISTENER(wl_touch)
#undef INTERN_LISTENER
    template <class WL_CLIENT>
    auto add_listener(WL_CLIENT* client,
                      typename wl_listener<WL_CLIENT>::type&& listener,
                      void* data = nullptr,
                      aux::slocation loc = aux::slocation::current()) {
        if (0 != wl_proxy_add_listener(reinterpret_cast<wl_proxy*>(client),
                                       reinterpret_cast<void(**)(void)>(&listener),
                                       data)) {
            throw loc;
        }
        return listener;
    }

    template <class> constexpr wl_interface const *const interface_pointer = nullptr;
#define INTERN_INTERFACE(WL_CLIENT)                                     \
    template <> constexpr wl_interface const *const interface_pointer<WL_CLIENT> = &WL_CLIENT##_interface;
    INTERN_INTERFACE(wl_compositor)
    INTERN_INTERFACE(wl_seat)
    INTERN_INTERFACE(zxdg_shell_v6)
    INTERN_INTERFACE(zwp_tablet_manager_v2)
#undef INTERN_INTERFACE

    template <class... WL_GLOBALS>
    auto register_globals(wl_display* display, wl_registry* registry) {
        constexpr size_t N = sizeof... (WL_GLOBALS);
        constexpr auto interfaces = aux::chunk {
            interface_pointer<WL_GLOBALS>...
        };
        std::tuple<WL_GLOBALS*...> results_raw;
        auto results_raw_ref = [&]<size_t... I>(std::index_sequence<I...>) {
            return aux::chunk {
                reinterpret_cast<void**>(&std::get<I>(results_raw))...
            };
        } (std::index_sequence_for<WL_GLOBALS...>());

        auto listener = add_listener(registry, {
                .global = aux::lamed([&](auto, auto, uint32_t name, std::string_view interface, uint32_t version) {
                    for (size_t i = 0; i < N; ++i) {
                        if (interface == interfaces[i]->name) {
                            *(results_raw_ref[i]) = wl_registry_bind(registry, name, interfaces[i], version);
                            break;
                        }
                    }
                }),
                .global_remove = aux::lamed(),
            });
        wl_display_roundtrip(display);
        return std::pair {
            listener,
            std::apply([](auto... raws) { return std::tuple{ safe_ptr(raws)... }; }, results_raw),
        };
    }
} // ::wayland_client_support

int main() {
    using namespace aux;

    try {
        auto display = safe_ptr(wl_display_connect(nullptr));
        auto registry = safe_ptr(wl_display_get_registry(display.get()));

        auto [registry_listener, globals] = register_globals<wl_compositor,
                                                             wl_seat,
                                                             zxdg_shell_v6,
                                                             zwp_tablet_manager_v2>(display.get(), registry.get());
        auto& [compositor, seat, shell, tablet] = globals;

        uint32_t seat_capabilities = 0;
        [[maybe_unused]]
        auto seat_listener = add_listener(seat.get(), {
                .capabilities = lamed([&](auto, auto, uint32_t capabilities) {
                    seat_capabilities = capabilities;
                }),
                .name = lamed(),
            });
        wl_display_roundtrip(display.get());
        if (!(seat_capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) {
            throw slocation::current();
        }

        auto keyboard = safe_ptr(wl_seat_get_keyboard(seat.get()));
        [[maybe_unused]]
        auto keyboard_listener = add_listener(keyboard.get(), {
                .keymap = lamed(),
                .enter = lamed(),
                .leave = lamed(),
                .key = lamed(),
                .modifiers = lamed(),
                .repeat_info = lamed(),
            });

        [[maybe_unused]]
        auto shell_listener = add_listener(shell.get(), {
                .ping = lamed([](auto, auto shell, uint32_t serial) noexcept {
                    zxdg_shell_v6_pong(shell, serial);
                }),
            });

        auto surface = safe_ptr(wl_compositor_create_surface(compositor.get()));
        auto xsurface = safe_ptr(zxdg_shell_v6_get_xdg_surface(shell.get(), surface.get()));
        [[maybe_unused]]
        auto xsurface_listener = add_listener(xsurface.get(), {
                .configure = lamed([](auto, auto xsurface, uint32_t serial) noexcept {
                    zxdg_surface_v6_ack_configure(xsurface, serial);
                }),
            });

        auto instance = vk::createInstanceUnique(
            VkInstanceCreateInfo {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = { },
                .pApplicationInfo = aux::chunk {
                    VkApplicationInfo {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pNext = nullptr,
                        .pApplicationName = "wl-client-vulkan",
                        .applicationVersion = VK_MAKE_VERSION(0, 0, 0),
                        .pEngineName = nullptr,
                        .engineVersion = VK_MAKE_VERSION(0, 0, 0),
                        .apiVersion = VK_API_VERSION_1_0,//VK_API_VERSION_1_3,
                    }
                },
                .enabledLayerCount = 2,
                .ppEnabledLayerNames = aux::chunk {
                    "VK_LAYER_KHRONOS_validation",
                    "VK_LAYER_LUNARG_api_dump"
                },
                .enabledExtensionCount = 4,
                .ppEnabledExtensionNames = aux::chunk {
                    "VK_KHR_surface",
                    "VK_KHR_wayland_surface",
                    "VK_EXT_debug_report",
                    "VK_EXT_debug_utils"
                },
            }
        );

        auto [physical_device, queue_family_index] = [&] {
            for (auto pdev : instance->enumeratePhysicalDevices()) {
                auto feat = pdev.getFeatures();
                if (feat.geometryShader == VK_TRUE && feat.tessellationShader == VK_TRUE) {
                    for (uint32_t index = 0; auto qprop : pdev.getQueueFamilyProperties()) {
                        if ((qprop.queueFlags & vk::QueueFlagBits::eGraphics) &&
                            (qprop.queueFlags & vk::QueueFlagBits::eCompute) &&
                            (qprop.queueFlags & vk::QueueFlagBits::eTransfer))
                        {
                            return std::tuple{pdev, index};
                        }
                        ++index;
                    }
                }
            }
            throw slocation::current();
        }();

        auto device = physical_device.createDeviceUnique(
            VkDeviceCreateInfo {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .pNext = nullptr,
                .flags = { },
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = aux::chunk {
                    VkDeviceQueueCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .pNext = nullptr,
                        .flags = { },
                        .queueFamilyIndex = queue_family_index,
                        .queueCount = 1,
                        .pQueuePriorities = aux::chunk {
                            0.0f,
                        },
                    },
                },
                .enabledLayerCount = 0,
                .ppEnabledLayerNames = nullptr,
                .enabledExtensionCount = 1,
                .ppEnabledExtensionNames = aux::chunk {
                    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                },
                .pEnabledFeatures = aux::chunk {
                    VkPhysicalDeviceFeatures{ },
                },
            }
        );

        // auto semaphore1 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        // auto semaphore2 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        // auto fence1 = device->createFenceUnique(vk::FenceCreateInfo {
        //         vk::FenceCreateFlagBits::eSignaled,
        //     });
        // auto semaphore3 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        // auto semaphore4 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        // auto fence2 = device->createFenceUnique(vk::FenceCreateInfo {
        //         vk::FenceCreateFlagBits::eSignaled,
        //     });

        auto vk_surface = instance->createWaylandSurfaceKHRUnique(
            VkWaylandSurfaceCreateInfoKHR {
                .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                .pNext = nullptr,
                .flags = {},
                .display = display.get(),
                .surface = surface.get(),
            }
        );

        if (!physical_device.getSurfaceSupportKHR(queue_family_index, vk_surface.get())) {
            throw slocation::current();
        }
        // auto formats = physical_device.getSurfaceFormatsKHR(vk_surface.get());
        auto pdev_properties = physical_device.getProperties();
        auto pdev_memory_properties = physical_device.getMemoryProperties();

        std::vector<VkDrawIndexedIndirectCommand> draw_commands;
        uint32_t first_index = 0u;
        int32_t vertex_offset = 0;
        uint64_t vbuffer_size = 0ull;
        uint64_t ibuffer_size = 0ull;
        for (auto const mesh : builtin::meshes) {
            draw_commands.push_back(
                VkDrawIndexedIndirectCommand {
                    .indexCount = static_cast<uint32_t>(3 * mesh.indices.size()),
                    .instanceCount = 1,
                    .firstIndex = first_index,
                    .vertexOffset = vertex_offset,
                    .firstInstance = 0,
                }
            );
            first_index += static_cast<uint32_t>(3 * mesh.indices.size());
            vertex_offset += static_cast<uint32_t>(mesh.vertices.size());
            vbuffer_size += mesh.vertices.size() * 24;
            ibuffer_size += mesh.indices.size() * 12;
        }

        auto vbuffer = device->createBufferUnique(
            VkBufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = { },
                .size = vbuffer_size,
                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = queue_family_index,
                .pQueueFamilyIndices = nullptr,
            }
        );
        auto ibuffer = device->createBufferUnique(
            VkBufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = { },
                .size = ibuffer_size,
                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = queue_family_index,
                .pQueueFamilyIndices = nullptr,
            }
        );
        auto vbuffer_requirements = device->getBufferMemoryRequirements(vbuffer.get());
        auto ibuffer_requirements = device->getBufferMemoryRequirements(ibuffer.get());

        auto ibuffer_offset = vbuffer_requirements.size
            + (ibuffer_requirements.alignment - (vbuffer_requirements.size %
                                                 ibuffer_requirements.alignment));

        auto memory = device->allocateMemoryUnique(
            VkMemoryAllocateInfo {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = ibuffer_offset + ibuffer_requirements.size,
                .memoryTypeIndex = 0,
            }
        );
        if (memory) {
            device->bindBufferMemory(vbuffer.get(), memory.get(), 0);
            device->bindBufferMemory(ibuffer.get(), memory.get(), ibuffer_offset);
            auto mapped = static_cast<uint8_t*>(device->mapMemory(memory.get(), 0, VK_WHOLE_SIZE, {}));
            for (size_t offset = 0; auto mesh : builtin::meshes) {
                auto size = std::size(mesh.vertices) * sizeof (mesh.vertices[0]);
                std::memcpy(mapped + offset, mesh.vertices.data(), size);
                offset += size;
            }
            for (size_t offset = ibuffer_offset; auto mesh : builtin::meshes) {
                auto size = std::size(mesh.indices) * sizeof (mesh.indices[0]);
                std::memcpy(mapped + offset, mesh.indices.data(), size);
                offset += size;
            }
            device->unmapMemory(memory.get());
        }
        else {
            throw slocation::current();
        }

        // auto formats_properties = physical_device.getFormatProperties(vk::Format::eB8G8R8A8Srgb);

        auto renderpass = device->createRenderPassUnique(
            VkRenderPassCreateInfo {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .pNext = nullptr,
                .flags = { },
                .attachmentCount = 1,
                .pAttachments = aux::chunk {
                    VkAttachmentDescription {
                        .flags = { },
                        .format = VK_FORMAT_B8G8R8A8_SRGB,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    }
                },
                .subpassCount = 1,
                .pSubpasses = aux::chunk {
                    VkSubpassDescription {
                        .flags = { },
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .inputAttachmentCount = 0,
                        .pInputAttachments = nullptr,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = aux::chunk {
                            VkAttachmentReference {
                                .attachment = 0,
                                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            },
                        },
                        .pResolveAttachments = nullptr,
                        .pDepthStencilAttachment = nullptr,
                        .preserveAttachmentCount = 0,
                        .pPreserveAttachments = nullptr,
                    },
                },
                .dependencyCount = 1,
                .pDependencies = aux::chunk {
                    VkSubpassDependency {
                        .srcSubpass = VK_SUBPASS_EXTERNAL,
                        .dstSubpass = 0,
                        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .srcAccessMask = VK_ACCESS_NONE,
                        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        .dependencyFlags = 0,
                    },
                },
            }
        );

        auto vshader = device->createShaderModuleUnique(
            VkShaderModuleCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .codeSize = std::size(builtin::vshader_binary) * sizeof (uint32_t),
                .pCode = std::data(builtin::vshader_binary),
            }
        );
        auto fshader = device->createShaderModuleUnique(
            VkShaderModuleCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .codeSize = std::size(builtin::fshader_binary) * sizeof (uint32_t),
                .pCode = std::data(builtin::fshader_binary),
            }
        );

        auto desc_set_layout = device->createDescriptorSetLayoutUnique(
            VkDescriptorSetLayoutCreateInfo {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .bindingCount = 1,
                .pBindings = aux::chunk {
                    VkDescriptorSetLayoutBinding {
                        .binding = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    },
                },
            }
        );

        auto pipeline_layout = device->createPipelineLayoutUnique(
            VkPipelineLayoutCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = aux::chunk {
                    VkDescriptorSetLayout {
                        desc_set_layout.get(),
                    },
                },
            }
        );

        /*
          VUID-VkGraphicsPipelineCreateInfo-layout-00756(ERROR / SPEC): msgNum: 1165064310
          - Validation Error: [ VUID-VkGraphicsPipelineCreateInfo-layout-00756 ]

          Object 0:
          handle = 0xf443490000000006,
          type = VK_OBJECT_TYPE_SHADER_MODULE;

          Object 1:
          handle = 0xee647e0000000009,
          type = VK_OBJECT_TYPE_PIPELINE_LAYOUT;

          | MessageID = 0x45717876 |
          Set 0 Binding 0 type mismatch on descriptor slot,
          uses type VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
          but expected VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC

          The Vulkan spec states: layout must be consistent with all shaders specified in pStages
          (https://vulkan.lunarg.com/doc/view/1.3.236.0/linux/1.3-extensions/vkspec.html#VUID-VkGraphicsPipelineCreateInfo-layout-00756)

          Objects: 2
          [0] 0xf443490000000006, type: 15, name: NULL
          [1] 0xee647e0000000009, type: 17, name: NULL
        */

        auto pipeline = device->createGraphicsPipelineUnique(
            nullptr,
            VkGraphicsPipelineCreateInfo {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = aux::chunk {
                    VkPipelineShaderStageCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .module = vshader.get(),
                        .pName = "main",
                    },
                    VkPipelineShaderStageCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .module = fshader.get(),
                        .pName = "main",
                    }
                },
                .pVertexInputState = aux::chunk {
                    VkPipelineVertexInputStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                        .vertexBindingDescriptionCount = 1,
                        .pVertexBindingDescriptions = aux::chunk {
                            VkVertexInputBindingDescription {
                                .binding = 0,
                                .stride = 24,
                                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                            },
                        },
                        .vertexAttributeDescriptionCount = 2,
                        .pVertexAttributeDescriptions = aux::chunk {
                            VkVertexInputAttributeDescription {
                                .location = 0,
                                .binding = 0,
                                .format = VK_FORMAT_R32G32B32_SFLOAT,
                                .offset = 0,
                            },
                            VkVertexInputAttributeDescription {
                                .location = 1,
                                .binding = 0,
                                .format = VK_FORMAT_R32G32B32_SFLOAT,
                                .offset = 12,
                            },
                        },
                    },
                },
                .pInputAssemblyState = aux::chunk {
                    VkPipelineInputAssemblyStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                        .primitiveRestartEnable = false,
                    },
                },
                .pTessellationState = nullptr,
                .pViewportState = aux::chunk {
                    VkPipelineViewportStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                        .viewportCount = 1,
                        .scissorCount = 1,
                    },
                },
                .pRasterizationState = aux::chunk {
                    VkPipelineRasterizationStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                        .depthClampEnable = false,
                        .rasterizerDiscardEnable = false,
                        .polygonMode = VK_POLYGON_MODE_FILL,
                        .cullMode = VK_CULL_MODE_NONE,
                        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                        .depthBiasEnable = false,
                        .lineWidth = 1.0f,
                    },
                },
                .pMultisampleState = aux::chunk {
                    VkPipelineMultisampleStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                        .sampleShadingEnable = false,
                        .pSampleMask = nullptr,
                        .alphaToCoverageEnable = false,
                        .alphaToOneEnable = false,
                    },
                },
                .pDepthStencilState = nullptr,
                .pColorBlendState = aux::chunk {
                    VkPipelineColorBlendStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                        .logicOpEnable = false,
                        .attachmentCount = 1,
                        .pAttachments = aux::chunk {
                            VkPipelineColorBlendAttachmentState {
                                .blendEnable = true,
                                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                .colorBlendOp = VK_BLEND_OP_ADD,
                                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                                .alphaBlendOp = VK_BLEND_OP_ADD,
                                .colorWriteMask
                                = VK_COLOR_COMPONENT_R_BIT
                                | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT
                                | VK_COLOR_COMPONENT_A_BIT,
                            },
                        },
                    },
                },
                .pDynamicState = aux::chunk {
                    VkPipelineDynamicStateCreateInfo {
                        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                        .dynamicStateCount = 2,
                        .pDynamicStates = aux::chunk {
                            VK_DYNAMIC_STATE_VIEWPORT,
                            VK_DYNAMIC_STATE_SCISSOR,
                        },
                    },
                },
                .layout = pipeline_layout.get(),
                .renderPass = renderpass.get(),
            }
        );

        // struct frame {
        //     VkFence fence;
        //     VkCommandBuffer primary;
        //     std::vector<VkCommandBuffer> worker;
        //     VkBuffer buf;
        //     uint8_t* base;
        //     VkDescriptorSet desc;
        // };
        // auto nof_workers = std::thread::hardware_concurrency();

        auto create_fence_signaled = [&] {
            return device->createFenceUnique(
                VkFenceCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
                }
            );
        };
        auto frame_fences = aux::chunk {
            create_fence_signaled(),
            create_fence_signaled(),
        };

        // auto const nof_workers = std::thread::hardware_concurrency();
        auto create_command_pool_and_buffer = [&](auto queue_family_index,
                                                  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
            auto pool = device->createCommandPoolUnique(
                VkCommandPoolCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                    .queueFamilyIndex = queue_family_index,
                }
            );
            auto buffers = device->allocateCommandBuffers(
                VkCommandBufferAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                    .commandPool = pool.get(),
                    .level = level,
                    .commandBufferCount = 2,
                }
            );
            return std::tuple {
                std::move(pool),
                std::move(buffers),
            };
        };
        auto frame_command_pools = aux::chunk {
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index),
            create_command_pool_and_buffer(queue_family_index, VK_COMMAND_BUFFER_LEVEL_PRIMARY),
        };

        constexpr static auto align_size = [](size_t s, size_t a) noexcept {
            return ((s + a - 1) / a) * a;
        };

        struct shader_param_block {
            aux::vec4f light_pos;
            aux::vec4f light_color;
            aux::versor<float, 16> model;
            aux::versor<float, 16> view_projection;
            float allpha;
        };
        constexpr auto nof_objects_per_frame = 5000;
        auto const pdev_uniform_align = pdev_properties.limits.minUniformBufferOffsetAlignment;
        auto const shader_param_block_aligned_size = align_size(sizeof (shader_param_block), pdev_uniform_align);
        auto create_parameter_buffer = [&] {
            return device->createBufferUnique(
                VkBufferCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = shader_param_block_aligned_size * nof_objects_per_frame,
                    .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                }
            );
        };
        auto buffer_per_frames = aux::chunk {
            create_parameter_buffer(),
            create_parameter_buffer(),
        };

        auto mem_requirements = device->getBufferMemoryRequirements(buffer_per_frames.front().get());
        auto mem_entire_frames = [&] {
            for (uint32_t i = 0; i < pdev_memory_properties.memoryTypeCount; ++i) {
                auto flags = static_cast<VkMemoryPropertyFlags>(pdev_memory_properties.memoryTypes[i].propertyFlags);
                if ((mem_requirements.memoryTypeBits & (1 << i)) &&
                    (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                    (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                {
                    return device->allocateMemoryUnique(
                        VkMemoryAllocateInfo {
                            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                            .pNext = nullptr,
                            .allocationSize = 2 * mem_requirements.size,
                            .memoryTypeIndex = i,
                        }
                    );
                }
            }
            throw slocation::current();
        }();

        auto mem_entire_frame_map = aux::safe_ptr(device->mapMemory(mem_entire_frames.get(), 0, VK_WHOLE_SIZE),
                                                  [&](auto) noexcept {
                                                      device->unmapMemory(mem_entire_frames.get());
                                                  });
        device->bindBufferMemory(buffer_per_frames[0].get(), mem_entire_frames.get(), 0);
        device->bindBufferMemory(buffer_per_frames[1].get(), mem_entire_frames.get(), mem_requirements.size);

        auto objects_per_frame = reinterpret_cast<std::array<std::array<shader_param_block,
                                                                        nof_objects_per_frame>, 2>*>
            (mem_entire_frame_map.get());

        // auto begin_ = VkRenderPassBeginInfo {
        //     .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        //     .renderPass = renderpass.get(),
        //     .clearValueCount = 1,
        //     .pClearValues = VkClearValue {
        //         .color = VkClearColorValue {
        //             .float32 = { 0.0f, 0.1f, 0.2f, 1.0f, },
        //         },
        //         .depthStencil = { },
        //     },
        // };
    }
    catch (std::exception& ex) {
        std::cerr << "caught standard exception: " << ex.what() << std::endl;
    }
    catch (slocation& loc) {
        std::cerr << loc.file_name() << ':' << loc.line() << ": fatal error..." << std::endl;
    }

    return 0;
}
