
#include <concepts>
#include <iostream>
#include <memory>
#include <tuple>
#include <utility>
#include <initializer_list>
#include <source_location>
#include <complex>
#include <vector>

#include <cstdint>

#include <wayland-client.h>
#include "xdg-shell-v6-client.h"
#include "zwp-tablet-v2-client.h"

#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.hpp>

inline namespace aux
{
    namespace detail
    {
        template <class Ch, class Tuple, size_t... I>
        void print(std::basic_ostream<Ch>& output, Tuple const& tuple, std::index_sequence<I...>) noexcept {
            (void) std::initializer_list<int>{(output << (I == 0 ? "" : " ") << std::get<I>(tuple), 0)... };
        }
    } // ::detail

    template <class Ch, class... Args>
    auto& operator<<(std::basic_ostream<Ch>& output, std::tuple<Args...> const& tuple) noexcept {
        output.put('(');
        detail::print(output, tuple, std::make_index_sequence<sizeof... (Args)>());
        output.put(')');
        return output;
    }

    template <class T, class D>
    auto safe_ptr(T* ptr, D del, std::source_location loc = std::source_location::current()) {
        if (ptr == nullptr) throw loc;
        return std::unique_ptr<T, D>(ptr, del);
    }
} // ::aux

inline namespace wl_client_topic_support
{
    namespace detail
    {
        template <class CLOSURE_TUPLE, size_t I>
        void to_function(void* data, auto, auto... args) {
            std::get<I>(*reinterpret_cast<CLOSURE_TUPLE*>(data))(args...);
        }
        template <class WL_LISTENER, class CLOSURE_TUPLE, size_t... I>
        [[nodiscard]] auto bind_listener(CLOSURE_TUPLE closure, std::index_sequence<I...>) {
            struct {
                WL_LISTENER listener;
                CLOSURE_TUPLE closure;
            } binder {
                .listener { to_function<CLOSURE_TUPLE, I>... },
                .closure { closure }
            };
            return binder;
        }
    }
    template <class WL_LISTENER, class... CLOSURE>
    [[nodiscard]] auto bind_listener(CLOSURE... closure) {
        return detail::bind_listener<WL_LISTENER>(std::tuple(closure...),
                                                  std::make_index_sequence<sizeof... (CLOSURE)>());
    }
} // ::wl_client_topic_support

int main() {
    try {
        auto display = safe_ptr(wl_display_connect(nullptr), wl_display_disconnect);
        auto registry = safe_ptr(wl_display_get_registry(display.get()), wl_registry_destroy);

        void* compositor_raw = nullptr;
        void* seat_raw = nullptr;
        void* shell_raw = nullptr;
        void* tablet_manager_raw = nullptr;

        auto registry_binder = bind_listener<wl_registry_listener>(
            [&](uint32_t name, std::string_view interface, uint32_t version) noexcept { // global
                if (interface == wl_compositor_interface.name) {
                    compositor_raw = wl_registry_bind(registry.get(),
                                                      name,
                                                      &wl_compositor_interface,
                                                      version);
                }
                else if (interface == wl_seat_interface.name) {
                    seat_raw = wl_registry_bind(registry.get(),
                                                name,
                                                &wl_seat_interface,
                                                version);
                }
                else if (interface == zxdg_shell_v6_interface.name) {
                    shell_raw = wl_registry_bind(registry.get(),
                                                 name,
                                                 &zxdg_shell_v6_interface,
                                                 version);
                }
                else if (interface == zwp_tablet_manager_v2_interface.name) {
                    tablet_manager_raw = wl_registry_bind(registry.get(),
                                                  name,
                                                  &zwp_tablet_manager_v2_interface,
                                                  version);
                }
            },
            [&](uint32_t) { // global_remove
                throw std::source_location::current();
            });
        wl_registry_add_listener(registry.get(), &registry_binder.listener, &registry_binder.closure);
        wl_display_roundtrip(display.get());

        auto compositor = safe_ptr(reinterpret_cast<wl_compositor*>(compositor_raw), wl_compositor_destroy);
        auto seat = safe_ptr(reinterpret_cast<wl_seat*>(seat_raw), wl_seat_destroy);
        auto shell = safe_ptr(reinterpret_cast<zxdg_shell_v6*>(shell_raw), zxdg_shell_v6_destroy);
        auto tablet_manager = safe_ptr(reinterpret_cast<zwp_tablet_manager_v2*>(tablet_manager_raw),
                                       zwp_tablet_manager_v2_destroy);

        auto shell_binder = bind_listener<zxdg_shell_v6_listener>(
            [&](uint32_t serial) noexcept { // ping
                zxdg_shell_v6_pong(shell.get(), serial);
            });
        zxdg_shell_v6_add_listener(shell.get(), &shell_binder.listener, &shell_binder.closure);

        auto surface = safe_ptr(wl_compositor_create_surface(compositor.get()), wl_surface_destroy);
        auto xsurface = safe_ptr(zxdg_shell_v6_get_xdg_surface(shell.get(), surface.get()), zxdg_surface_v6_destroy);
        auto xsurface_binder = bind_listener<zxdg_surface_v6_listener>(
            [&](uint32_t serial) noexcept { // configure
                zxdg_surface_v6_ack_configure(xsurface.get(), serial);
            });
        zxdg_surface_v6_add_listener(xsurface.get(), &xsurface_binder.listener, &xsurface_binder.closure);

        ///////////////////////////////////////////////////////
        vk::ApplicationInfo app_info {
            "wl-client-vulkan",
            0,                  // VK_MAKE_VERSION(1, 0, 0),
            nullptr,            //"No engine",
            0,                  // VK_MAKE_VERSION(1, 0, 0),
            VK_API_VERSION_1_3,
        };
        constexpr char const* layers[] {
            "VK_LAYER_KHRONOS_validation",
            "VK_LAYER_LUNARG_api_dump",
        };
        constexpr char const* extensions[] {
            "VK_KHR_surface",
            "VK_KHR_wayland_surface",
            "VK_EXT_debug_report",
            "VK_EXT_debug_utils",
        };

        vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> createInfo {
            { {}, &app_info, layers, extensions },
            { {},
              vk::DebugUtilsMessageSeverityFlagsEXT {
                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
              },
              vk::DebugUtilsMessageTypeFlagsEXT {
                  vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                  vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                  vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                  vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding
              },
              [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                 VkDebugUtilsMessageTypeFlagsEXT type,
                 VkDebugUtilsMessengerCallbackDataEXT const* callback_data,
                 void* userdata) noexcept {
                  // std::cerr << "validation layer: " << callback_data->pMessage << std::endl;
                  return VK_FALSE;
              },
            },
        };
        auto instance = vk::createInstanceUnique(createInfo.get<vk::InstanceCreateInfo>());

        auto device = [&instance] {
            for (auto pdev : instance->enumeratePhysicalDevices()) {
                auto feat = pdev.getFeatures();
                if (feat.geometryShader == VK_TRUE && feat.tessellationShader == VK_TRUE) {
                    for (uint32_t index = 0; auto qprop : pdev.getQueueFamilyProperties()) {
                        if (qprop.queueFlags & vk::QueueFlagBits::eGraphics &&
                            qprop.queueFlags & vk::QueueFlagBits::eCompute &&
                            qprop.queueFlags & vk::QueueFlagBits::eTransfer)
                        {
                            float queue_priorities[] = { 0.0f };
                            vk::DeviceQueueCreateInfo info {
                                {}, index, 1, queue_priorities,
                            };
                            constexpr char const* layers[] = {
                                // "VK_LAYER_KHRONOS_validation",
                                // "VK_LAYER_LUNARG_api_dump",
                            };
                            constexpr char const* extensions[] = {
                                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                // VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                                // VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                                // VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                // VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                            };
                            vk::PhysicalDeviceFeatures feat_null{};
                            return pdev.createDeviceUnique(vk::DeviceCreateInfo {
                                    {},
                                    1,
                                    &info,
                                    0,
                                    layers,
                                    1,
                                    extensions,
                                    &feat_null
                                });
                        }
                        ++index;
                    }
                }
            }
            throw std::source_location::current();
        }();

        auto semaphore1 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        auto semaphore2 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        auto fence1 = device->createFenceUnique(vk::FenceCreateInfo {
                vk::FenceCreateFlagBits::eSignaled,
            });
        auto semaphore3 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        auto semaphore4 = device->createSemaphoreUnique(vk::SemaphoreCreateInfo {});
        auto fence2 = device->createFenceUnique(vk::FenceCreateInfo {
                vk::FenceCreateFlagBits::eSignaled,
            });

        auto vk_surface = instance->createWaylandSurfaceKHRUnique({
                {},
                display.get(),
                surface.get(),
                nullptr,
            });

        auto swapchain = device->createSwapchainKHRUnique({
                {},
                vk_surface.get(),
                4,
                vk::Format::eB8G8R8A8Unorm,
                vk::ColorSpaceKHR::eSrgbNonlinear,
                { 800, 600 },
                1,
                vk::ImageUsageFlagBits::eColorAttachment,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::SurfaceTransformFlagBitsKHR::eIdentity,
                vk::CompositeAlphaFlagBitsKHR::eOpaque,
                vk::PresentModeKHR::eMailbox,
                VK_TRUE,
            });

        auto images = device->getSwapchainImagesKHR(swapchain.get());
        // auto get_next_image = [&] {
        //     for (;;) {
        //         auto ret = device->acquireNextImageKHR(swapchain.get(), 0);
        //         if (ret.result == vk::Result::eSuccess) {
        //             return images[ret.value];
        //         }
        //         else if (ret.result != vk::Result::eNotReady) {
        //             throw std::source_location::current();
        //         }
        //     }
        // };
        // auto next_image = get_next_image();
        // std::cout << typeid (next_image).name() << ':' << next_image << std::endl;

        std::vector<decltype (device->createImageViewUnique({}))> views;
        for (auto image : images) {
            views.push_back(device->createImageViewUnique(vk::ImageViewCreateInfo {
                        {},
                        image,
                        vk::ImageViewType::e2D,
                        vk::Format::eB8G8R8A8Unorm,
                        {},
                        vk::ImageSubresourceRange {
                            vk::ImageAspectFlagBits::eColor,
                            0,
                            0,
                            0,
                            1,
                        },
                        nullptr,
                    }));
        }

        vk::AttachmentDescription attachment {
            vk::Flags<vk::AttachmentDescriptionFlagBits> { },
            vk::Format::eB8G8R8A8Unorm,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::ePresentSrcKHR,
        };
        vk::AttachmentReference attachment_ref {
            0,
            vk::ImageLayout::eColorAttachmentOptimal,
        };
        vk::SubpassDescription subpass {
            vk::Flags<vk::SubpassDescriptionFlagBits> { },
            vk::PipelineBindPoint::eGraphics,
            nullptr,
            attachment_ref,
        };
        vk::SubpassDependency dependency {
            VK_SUBPASS_EXTERNAL,
            0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlagBits::eNone,
            vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
        };
        auto renderpass = device->createRenderPassUnique(vk::RenderPassCreateInfo {
                vk::Flags<vk::RenderPassCreateFlagBits> { },
                attachment,
                subpass,
                dependency,
                nullptr,
            });

        vk::DescriptorSetLayoutBinding bindings[] {
            {
                0,
                vk::DescriptorType::eUniformBuffer,
                1,
                vk::ShaderStageFlagBits::eVertex,
                nullptr,
            },
            {
                1,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                vk::ShaderStageFlagBits::eFragment,
                nullptr,
            },
        };
        auto layout = device->createDescriptorSetLayoutUnique({
                { },
                static_cast<uint32_t>(std::size(bindings)),
                std::data(bindings),
            });

        constexpr static std::string_view vertex_code = R"(
            #version 450
            #extension GL_ARB_separate_shader_objects : enable
            #extension GL_NV_gpu_shader5 : enable

            layout(binding = 0) uniform UniformBufferObject {
                mat4 model;
                mat4 view;
                mat4 proj;
            } ubo;

            layout(location = 0) in vec4 inPosition;
            layout(location = 1) in vec3 inColor;
            layout(location = 2) in vec2 inTexCoord;

            layout(location = 0) out vec3 fragColor;
            layout(location = 1) out vec2 fragTexCoord;

            out gl_PerVertex {
                vec4 gl_Position;
            };

            void main() {
                gl_Position = ubo.proj * ubo.view * ubo.model * inPosition;
                fragColor = inColor;
                fragTexCoord = inTexCoord;
            }
        )";
        auto vertex_shader = device->createShaderModuleUnique(
            vk::ShaderModuleCreateInfo {
                {},
                vertex_code.size(),
                reinterpret_cast<uint32_t const*>(vertex_code.data()),
            });

        constexpr static std::string_view fragment_code = R"(
            #version 450
            #extension GL_ARB_separate_shader_objects : enable
            #extension GL_NV_gpu_shader5 : enable

            layout(location = 0) in vec3 fragColor;
            layout(location = 1) in vec2 fragTexCoord;
            layout(binding = 1) uniform sampler2D texSampler;

            layout(location = 0) out vec4 outColor;

            void main() {
                outColor = texture(texSampler, fragTexCoord);
            }
        )";
        auto fragment_shader = device->createShaderModuleUnique(
            vk::ShaderModuleCreateInfo {
                {},
                fragment_code.size(),
                reinterpret_cast<uint32_t const*>(fragment_code.data()),
            });

        vk::PipelineShaderStageCreateInfo stages[] = {
            {
                {},
                vk::ShaderStageFlagBits::eVertex,
                vertex_shader.get(),
                "main",
            },
            {
                {},
                vk::ShaderStageFlagBits::eFragment,
                fragment_shader.get(),
                "main",
            },
        };

        struct vertex {
            float pos[4];
            float color[3];
            float tex_coords[2];
        };

        vk::VertexInputBindingDescription vertex_input_binding {
            0,
            sizeof (vertex),
            vk::VertexInputRate::eVertex,
        };
        vk::VertexInputAttributeDescription attribute_descriptions[3] = {
            {
                0,
                0,
                vk::Format::eR32G32B32A32Sfloat,
                offsetof(vertex, pos),
            },
            {
                1,
                0,
                vk::Format::eR32G32B32Sfloat,
                offsetof(vertex, color),
            },
            {
                2,
                0,
                vk::Format::eR32G32Sfloat,
                offsetof(vertex, tex_coords),
            },
        };

        vk::PipelineVertexInputStateCreateInfo vertex_input_info {
            {},
            vertex_input_binding,
            attribute_descriptions,
        };

        vk::PipelineInputAssemblyStateCreateInfo input_assembly {
            {},
            vk::PrimitiveTopology::eTriangleList,
            VK_FALSE,
        };

        vk::Viewport viewport {
            0.0f,
            0.0f,
            800.0f,
            600.0f,
            0.0f,
            1.0f,
        };
        vk::Rect2D scissor {
            { 0, 0, },
            { 800, 600, },
        };
        vk::PipelineViewportStateCreateInfo viewport_state {
            {},
            viewport,
            scissor,
        };

        vk::PipelineRasterizationStateCreateInfo rasterization_state {
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eBack,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };

        vk::PipelineMultisampleStateCreateInfo multisample_state {
            {},
            vk::SampleCountFlagBits::e1,
            VK_FALSE,
        };

        vk::PipelineColorBlendAttachmentState color_blend_attachment { };
        color_blend_attachment.colorWriteMask =
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo color_blend_state { };
        color_blend_state.logicOp = vk::LogicOp::eCopy;
        color_blend_state.attachmentCount = 1;
        color_blend_state.pAttachments = &color_blend_attachment;

        vk::PipelineLayoutCreateInfo layout_info {
            {},
            layout.get(),
        };
        auto pipeline_layout = device->createPipelineLayoutUnique(layout_info);

        vk::GraphicsPipelineCreateInfo pipeline_info {
            {},
            stages,
            &vertex_input_info,
            &input_assembly,
            {}, // tessellation_state
            &viewport_state,
            &rasterization_state,
            &multisample_state,
            {},
            &color_blend_state,
            {},
            pipeline_layout.get(),
            renderpass.get(),
        };
        auto pipeline = device->createGraphicsPipelinesUnique(VK_NULL_HANDLE,
                                                              pipeline_info);

        ///////////////////////////////////////////////////////

        auto egl_display = safe_ptr(eglGetDisplay(display.get()), eglTerminate);
        eglInitialize(egl_display.get(), nullptr, nullptr);
        eglBindAPI(EGL_OPENGL_ES_API);
        auto egl_window = safe_ptr(wl_egl_window_create(surface.get(), 800, 600),
                                   wl_egl_window_destroy);
        EGLConfig config;
        EGLint num_config;
        eglChooseConfig(egl_display.get(),
                        std::array<EGLint, 15>(
                            {
                                EGL_LEVEL, 0,
                                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                EGL_RED_SIZE, 8,
                                EGL_GREEN_SIZE, 8,
                                EGL_BLUE_SIZE, 8,
                                EGL_ALPHA_SIZE, 8,
                                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                EGL_NONE,
                            }
                        ).data(),
                        &config, 1, &num_config);
        auto egl_context = safe_ptr(eglCreateContext(egl_display.get(),
                                                     config,
                                                     EGL_NO_CONTEXT,
                                                     std::array<EGLint, 3>(
                                                         {
                                                             EGL_CONTEXT_CLIENT_VERSION, 2,
                                                             EGL_NONE,
                                                         }
                                                     ).data()),
                                    [&egl_display](auto ptr) noexcept {
                                        eglDestroyContext(egl_display.get(), ptr);
                                    });
        auto egl_surface = safe_ptr(eglCreateWindowSurface(egl_display.get(),
                                                           config,
                                                           egl_window.get(),
                                                           nullptr),
                                    [&egl_display](auto ptr) noexcept {
                                        eglDestroySurface(egl_display.get(), ptr);
                                    });
        eglMakeCurrent(egl_display.get(),
                       egl_surface.get(), egl_surface.get(),
                       egl_context.get());

        struct {
            struct {
                std::complex<int32_t> configuration;
            } toplevel;
            struct {
                std::tuple<uint32_t, uint32_t> key;
            } keyboard;
            struct {
                std::complex<double> motion;
            } pointer;
            struct {
                std::array<std::complex<double>, 10> motions;
            } touch;
        } input;

        auto toplevel = safe_ptr(zxdg_surface_v6_get_toplevel(xsurface.get()), zxdg_toplevel_v6_destroy);
        auto toplevel_binder = bind_listener<zxdg_toplevel_v6_listener>(
            [&](int32_t width, int32_t height, auto) noexcept { // configure
                if (width * height) {
                    wl_egl_window_resize(egl_window.get(), width, height, 0, 0);
                    glViewport(0, 0, width, height);
                    input.toplevel.configuration = { width, height };
                }
            },
            [&]() noexcept { });//close
        zxdg_toplevel_v6_add_listener(toplevel.get(), &toplevel_binder.listener, &toplevel_binder.closure);
        wl_surface_commit(surface.get());

        auto keyboard = safe_ptr(wl_seat_get_keyboard(seat.get()), wl_keyboard_destroy);
        auto keyboard_binder = bind_listener<wl_keyboard_listener>(
            [](auto...) noexcept { }, // keymap
            [](auto...) noexcept { }, // enter
            [](auto...) noexcept { }, // leave
            [&](auto, auto, uint32_t key, uint32_t state) noexcept { // key
                input.keyboard.key = { key, state };
            },
            [](auto...) noexcept { }, // modifier
            [](auto...) noexcept { });// repeat_info
        wl_keyboard_add_listener(keyboard.get(), &keyboard_binder.listener, &keyboard_binder.closure);

        auto pointer = safe_ptr(wl_seat_get_pointer(seat.get()), wl_pointer_destroy);
        auto pointer_binder = bind_listener<wl_pointer_listener>(
            [](auto...) noexcept { }, // enter
            [](auto...) noexcept { }, // leave
            [&](auto, wl_fixed_t x, wl_fixed_t y) noexcept { // motion
                input.pointer.motion = { wl_fixed_to_double(x), wl_fixed_to_double(y) };
            },
            [](auto...) noexcept { }, // button
            [](auto...) noexcept { }, // axis
            [](auto...) noexcept { }, // frame
            [](auto...) noexcept { }, // axis_source
            [](auto...) noexcept { }, // axis_stop
            [](auto...) noexcept { });// axis_discrete
        wl_pointer_add_listener(pointer.get(), &pointer_binder.listener, &pointer_binder.closure);

        auto touch = safe_ptr(wl_seat_get_touch(seat.get()), wl_touch_destroy);
        auto touch_binder = bind_listener<wl_touch_listener>(
            [](auto...) noexcept { }, // down
            [](auto...) noexcept { }, // up
            [&](auto, int32_t id, wl_fixed_t x, wl_fixed_t y) noexcept { // motion
                input.touch.motions[id] = { wl_fixed_to_double(x), wl_fixed_to_double(y) };
            },
            [](auto...) noexcept { }, // frame
            [](auto...) noexcept { }, // cancel
            [](auto...) noexcept { }, // shape
            [](auto...) noexcept { });// orientation
        wl_touch_add_listener(touch.get(), &touch_binder.listener, &touch_binder.closure);

        // auto tablet_seat = safe_ptr(zwp_tablet_manager_v2_get_tablet_seat(tablet_manager.get(), seat.get()),
        //                             zwp_tablet_seat_v2_destroy);
        // zwp_tablet_v2* tablet_raw = nullptr;
        // zwp_tablet_tool_v2* tablet_tool_raw = nullptr;
        // zwp_tablet_pad_v2* tablet_pad_raw = nullptr;
        // auto tablet_seat_binder = bind_listener<zwp_tablet_seat_v2_listener>(
        //     [&](auto tablet) noexcept { // tablet_added
        //         tablet_raw = tablet;
        //     },
        //     [&](auto tablet_tool) noexcept { // tool_added
        //         tablet_tool_raw = tablet_tool;
        //     },
        //     [&](auto tablet_pad) noexcept { // pad_added
        //         tablet_pad_raw = tablet_pad;
        //     });
        // zwp_tablet_seat_v2_add_listener(tablet_seat.get(),
        //                                 &tablet_seat_binder.listener,
        //                                 &tablet_seat_binder.closure);
        // wl_display_roundtrip(display.get());
        // auto tablet = safe_ptr(tablet_raw, zwp_tablet_v2_destroy);

        while (wl_display_dispatch(display.get()) != -1) {
            if (input.keyboard.key == std::tuple{1, 0}) { // ESC-UP
                break;
            }
            glClearColor(0.0, 0.0, 0.8, 0.8);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            eglSwapBuffers(egl_display.get(), egl_surface.get());
        }

        while (vkDeviceWaitIdle(device.get()) != VK_SUCCESS) continue; // clean up wait
    }
    catch (std::source_location& loc) {
        std::cerr << loc.file_name() << ':' << loc.line() << ": Fatal error..." << std::endl;
    }
    catch (vk::Error& error) {
        std::cerr << error.what() << std::endl;
    }
    std::cout << "bye." << std::endl;
    return 0;
}
