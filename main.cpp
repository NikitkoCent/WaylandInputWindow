#include "utilities.h"      // WLResourceWrapper, makeWLResourceWrapperChecked, logging::*, MY_LOG_*
#include <wayland-client.h> // wl_*
#include <string>           // std::string
#include <string_view>      // std::string_view
#include <unordered_map>    // std::unordered_map
#include <optional>         // std::optional
#include <vector>           // std::vector
#include <utility>          // std::move


/** Holds the whole state required for the app functioning */
struct WLAppCtx
{
    WLResourceWrapper<wl_display*> connection;

    WLResourceWrapper<wl_registry*> registry;
    struct WLGlobalObjectInfo
    {
        std::string interface;
        uint32_t version;
        /** if non-empty, the object has already been binded and the value specifies the version used for binding */
        std::optional<uint32_t> bindedVersion;
    };
    std::unordered_map<uint32_t /* name */, WLGlobalObjectInfo> availableGlobalObjects;


    ~WLAppCtx() noexcept
    {
        // Keeping the correct order of the resources disposal

        availableGlobalObjects.clear();
        registry.reset();
        connection.reset();
    }
};


int main(int, char*[])
{
    try
    {
        WLAppCtx appCtx;

        // ========================== Step 1: make a connection to the Wayland server/compositor ======================
        appCtx.connection = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_display_connect(nullptr)),
            nullptr,
            [](auto& dsp) { MY_LOG_WLCALL(wl_display_disconnect(dsp)); dsp = nullptr; }
        );
        if (!appCtx.connection.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to connect to a Wayland compositor");

        const int connectionFd = MY_LOG_WLCALL(wl_display_get_fd(*appCtx.connection));
        MY_LOG_INFO("The Wayland connection fd: ", connectionFd, '.');
        // ================================================ END of step 1 =============================================

        // == Step 2: create and listen to a registry object to track any dynamic changes in the server configuration =
        appCtx.registry = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_display_get_registry(*appCtx.connection)),
            nullptr,
            [](auto& rgs) { MY_LOG_WLCALL(wl_registry_destroy(rgs)); rgs = nullptr; }
        );
        if (!appCtx.registry.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to obtain the registry global object");

        // TODO: why does it return 0?
        MY_LOG_INFO("The registry version: ", MY_LOG_WLCALL(wl_registry_get_version(*appCtx.registry)));

        struct RegistryListener
        {
            WLAppCtx& appCtx;
            const wl_registry_listener wlHandler = { &onGlobalEvent, &onGlobalRemoveEvent };

        public:
            using GlobalEventAppListener = std::function<void(uint32_t name, std::string_view interface, uint32_t version, const WLAppCtx& appCtx)>;
            using GlobalRemoveEventAppListener = std::function<void(uint32_t name, const WLAppCtx::WLGlobalObjectInfo& info, const WLAppCtx& appCtx)>;

        public:
            explicit RegistryListener(WLAppCtx& appCtx) noexcept
                : appCtx(appCtx)
            {}

        public:
            void addOnGlobalEventAppListener(GlobalEventAppListener listener)
            {
                MY_LOG_TRACE("registryListener::addOnGlobalEventAppListener.");

                registryGlobalEventsAppListeners_.emplace_back(std::move(listener));
            }

            void addOnGlobalRemoveEventAppListener(GlobalRemoveEventAppListener listener)
            {
                MY_LOG_TRACE("registryListener::addOnGlobalRemoveEventAppListener.");

                registryGlobalRemoveEventsAppListeners_.emplace_back(std::move(listener));
            }

        private:
            std::vector<GlobalEventAppListener> registryGlobalEventsAppListeners_;
            std::vector<GlobalRemoveEventAppListener> registryGlobalRemoveEventsAppListeners_;

        private:
            static void onGlobalEvent(void *data, wl_registry *registry, const uint32_t name, const char *interface, const uint32_t version)
            {
                MY_LOG_INFO(
                    "wl_registry::global: a new global object has been added to the server:\n"
                    "    data=", data, '\n',
                    "    wl_registry=", registry, '\n',
                    "    name=", name, '\n',
                    "    interface=\"", interface, "\"", '\n',
                    "    version=", version
                );

                RegistryListener& self = *static_cast<RegistryListener*>(data);
                const std::string_view interfaceSV = interface;

                if (const auto [iter, inserted] =
                    self.appCtx.availableGlobalObjects.insert_or_assign(
                        name,
                        WLAppCtx::WLGlobalObjectInfo{std::string { interfaceSV }, version, std::nullopt}
                    ) ; !inserted)
                {
                    MY_LOG_WARN("wl_registry::global: there already was a global object with name=", name, " ; rewritten.");
                }

                for (const auto& listener : self.registryGlobalEventsAppListeners_)
                {
                    listener(name, interfaceSV, version, self.appCtx);
                }
            }

            static void onGlobalRemoveEvent(void *data, wl_registry *registry, uint32_t name)
            {
                MY_LOG_INFO(
                    "wl_registry::global_remove: a global object has been removed from the server:\n"
                    "    data=", data, '\n',
                    "    wl_registry=", registry, '\n',
                    "    name=", name
                );

                RegistryListener& self = *static_cast<RegistryListener*>(data);

                const auto globalObjIter = self.appCtx.availableGlobalObjects.find(name);
                if (globalObjIter == self.appCtx.availableGlobalObjects.end()) {
                    MY_LOG_ERROR("onGlobalRemoveEvent: a global object with the name=", name, " has been removed, although it hadn't been added before.");
                    return;
                }

                const auto globalObjInfo = std::move(globalObjIter->second);
                self.appCtx.availableGlobalObjects.erase(globalObjIter);

                for (const auto& listener : self.registryGlobalRemoveEventsAppListeners_)
                {
                    listener(name, globalObjInfo, self.appCtx);
                }
            }
        } registryListener(appCtx);

        if (const auto err = MY_LOG_WLCALL(wl_registry_add_listener(*appCtx.registry, &registryListener.wlHandler, &registryListener)); err != 0)
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
        if (const auto err = MY_LOG_WLCALL(wl_display_roundtrip(*appCtx.connection)); err < 0)
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
