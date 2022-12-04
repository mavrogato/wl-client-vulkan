
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
    }
    template <class Ch, class... Args>
    auto& operator<<(std::basic_ostream<Ch>& output, std::tuple<Args...> const& tuple) noexcept {
        output.put('(');
        detail::print(output, tuple, std::make_index_sequence<sizeof... (Args)>());
        output.put(')');
        return output;
    }

    template <class T, class D>
    auto safe_ptr(T* ptr, D del, std::source_location loc = std::source_location::current()) {
        if (ptr == nullptr) {
            throw loc;
        }
        return std::unique_ptr<T, D>(ptr, del);
    }
} // ::aux

inline namespace wayland_client_support
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
            } binder = {
                .listener = {
                    to_function<CLOSURE_TUPLE, I>...
                },
                .closure = closure,
            };
            return binder;
        }
    }
    template <class WL_LISTENER, class... CLOSURE>
    [[nodiscard]] auto bind_listener(CLOSURE... closure) {
        return detail::bind_listener<WL_LISTENER>(std::tuple(closure...),
                                                  std::make_index_sequence<sizeof... (CLOSURE)>());
    }
} // ::wayland_client_support

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

        auto egl_display = safe_ptr(eglGetDisplay(display.get()), eglTerminate);
        eglInitialize(egl_display.get(), nullptr, nullptr);
        eglBindAPI(EGL_OPENGL_ES_API);
        auto egl_window = safe_ptr(wl_egl_window_create(surface.get(), 640, 480),
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
                std::array<std::complex<double>, 10> motion;
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
                input.touch.motion[id] = { wl_fixed_to_double(x), wl_fixed_to_double(y) };
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
            std::cout << input.toplevel.configuration << std::endl;
        }
    }
    catch (std::source_location& loc) {
        std::cerr << loc.file_name() << ':' << loc.line() << ": Fatal error..." << std::endl;
    }
    std::cout << "bye." << std::endl;
    return 0;
}
