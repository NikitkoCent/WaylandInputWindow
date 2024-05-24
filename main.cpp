#include "utilities.h"      // WLResourceWrapper, makeWLResourceWrapperChecked, logging::*, MY_LOG_*
#include <wayland-client.h> // wl_*


int main(int, char*[])
{
    try
    {
        // ========================== Step 1: make a connection to the Wayland server/compositor ======================
        const WLResourceWrapper<wl_display*> display = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_display_connect(nullptr)),
            nullptr,
            [](auto& dsp) { MY_LOG_WLCALL(wl_display_disconnect(dsp)); dsp = nullptr; }
        );
        if (!display.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to connect to a Wayland compositor");

        const int displayFd = MY_LOG_WLCALL(wl_display_get_fd(*display));
        MY_LOG_INFO("The Wayland connection fd: ", displayFd, '.');
        // ================================================ END of step 1 =============================================

        // == Step 2: create and listen to a registry object to track any dynamic changes in the server configuration =
        const WLResourceWrapper<wl_registry*> registry = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_display_get_registry(*display)),
            nullptr,
            [](auto& rgs) { MY_LOG_WLCALL(wl_registry_destroy(rgs)); rgs = nullptr; }
        );
        if (*registry == nullptr)
            throw std::system_error(errno, std::system_category(), "Failed to obtain the registry global object");

        // TODO: why does it return 0?
        MY_LOG_INFO("The registry version: ", MY_LOG_WLCALL(wl_registry_get_version(*registry)));

        struct {
            static void onGlobalEvent(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
            {
                MY_LOG_INFO(
                    "wl_registry::global: a new global object has been added to the server:\n"
                    "    data=", data, '\n',
                    "    wl_registry=", registry, '\n',
                    "    name=", name, '\n',
                    "    interface=\"", interface, "\"", '\n',
                    "    version=", version
                );
            }

            static void onGlobalRemoveEvent(void *data, wl_registry *registry, uint32_t name)
            {
                MY_LOG_INFO(
                    "wl_registry::global_remove: a global object has been removed from the server:\n"
                    "    data=", data, '\n',
                    "    wl_registry=", registry, '\n',
                    "    name=", name
                );
            }

            const wl_registry_listener wlHandler = { &onGlobalEvent, &onGlobalRemoveEvent };
        } registryListener;

        if (const auto err = MY_LOG_WLCALL(wl_registry_add_listener(*registry, &registryListener.wlHandler, &registryListener)); err != 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "Failed to set the registry listener (returned " + std::to_string(err) + ")"
            );
        // ============================================== END of step 2 ===============================================

        // Let's wait until the server has processed all our issued requests and libwayland-client has processed all
        //   the replies/events from the server.
        // This way we'll get all the initial wl_registry::global events, hence get aware about all the currently
        //   available global objects on the server.
        if (const auto err = MY_LOG_WLCALL(wl_display_roundtrip(*display)); err < 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "wl_display_roundtrip failed (returned " + std::to_string(err) + ")"
            );
    }
    catch (const std::system_error& err)
    {
        MY_LOG_ERROR("Caught an std::system_error: \"", err.what(), "\" (code=", err.code(), "). Shutting down...");
        return 1;
    }
    catch (const std::exception& err)
    {
        MY_LOG_ERROR("Caught an std::exception: \"", err.what(), "\". Shutting down...");
        return 2;
    }
    catch (...)
    {
        MY_LOG_ERROR("Caught an unknown exception. Shutting down...");
        return 3;
    }

    return 0;
}
