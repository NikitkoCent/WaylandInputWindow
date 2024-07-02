#include "utilities.h"      // WLResourceWrapper, makeWLResourceWrapperChecked, logging::*, MY_LOG_*
#include <wayland-client.h> // wl_*
#include <xdg-shell.h>      // xdg_*
#include <string>           // std::string
#include <string_view>      // std::string_view
#include <unordered_map>    // std::unordered_map
#include <optional>         // std::optional
#include <vector>           // std::vector
#include <utility>          // std::move
#include <limits>           // std::numeric_limits


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

    /** Responsible for creation of surfaces and regions */
    WLResourceWrapper<wl_compositor*> compositor;

    WLResourceWrapper<wl_shm*> shmProvider;

    // The entry point to the XDG shell protocol, responsible for assigning window roles to wl_surface instances
    //   and making them able to be dragged, resized, maximized, etc
    WLResourceWrapper<xdg_wm_base*> xdgShell;

    struct
    {
        const std::size_t width  = 800;
        const std::size_t height = 600;
        // XRGB8888 is used
        const std::size_t bytesPerPixel = 4;

        SharedMemoryBuffer surfaceSharedBuffer;
        WLResourceWrapper<wl_shm_pool*> surfaceBufferWLPool;
        WLResourceWrapper<wl_buffer*> surfaceWLSideBuffer1;
        WLResourceWrapper<wl_buffer*> surfaceWLSideBuffer2;
        /* 0 for surfaceWLSideBuffer1, 1 for surfaceWLSideBuffer2 */
        unsigned pendingBufferIdx = 0;

        [[nodiscard]] std::size_t getSurfaceBufferOffsetForIdx(unsigned idx) const noexcept
        {
            return idx * width * bytesPerPixel * height;
        }
        [[nodiscard]] std::size_t getSurfaceBufferPendingOffset() const noexcept
        {
            return getSurfaceBufferOffsetForIdx(pendingBufferIdx);
        }
        [[nodiscard]] wl_buffer* getPendingWLSideBuffer() const
        {
            return (pendingBufferIdx == 0) ? surfaceWLSideBuffer1.getResource() : surfaceWLSideBuffer2.getResource();
        }

        template<typename Visitor>
        void drawVia(
            Visitor&& v,
            const std::size_t rectX = 0,
            const std::size_t rectY = 0,
            std::size_t rectWidth = std::numeric_limits<std::size_t>::max(),
            std::size_t rectHeight = std::numeric_limits<std::size_t>::max()
        ) {
            MY_LOG_TRACE("mainWindow::drawVia: drawing into ", (pendingBufferIdx == 0) ? "1st" : "2nd", " buffer...");

            if ( (rectX >= 0 + width) || (rectY >= 0 + height) )
            {
                return;
            }
            rectWidth  = std::clamp<std::size_t>(rectWidth,  0, 0 + width  - rectX);
            rectHeight = std::clamp<std::size_t>(rectHeight, 0, 0 + height - rectY);
            const auto rectXMax = rectX + rectWidth;
            const auto rectYMax = rectY + rectHeight;

            const auto bufferToWriteOffset = getSurfaceBufferPendingOffset();

            for (std::size_t y = rectY; y < rectYMax; ++y)
            {
                const std::size_t rowOffset = bufferToWriteOffset + y * width * bytesPerPixel;

                for (std::size_t x = rectX; x < rectXMax; ++x)
                {
                    const std::size_t pixelOffset = rowOffset + x * bytesPerPixel;

                    // XRGB8888 format
                    std::byte& b = surfaceSharedBuffer[pixelOffset + 0];
                    std::byte& g = surfaceSharedBuffer[pixelOffset + 1];
                    std::byte& r = surfaceSharedBuffer[pixelOffset + 2];
                    std::byte& a = surfaceSharedBuffer[pixelOffset + 3];

                    std::forward<Visitor>(v)(x, y, b, g, r);
                    a = std::byte{ 0xFF };
                }
            }
        }

        WLResourceWrapper<wl_surface*> surface;

        WLResourceWrapper<xdg_surface*> xdgSurface;
        WLResourceWrapper<xdg_toplevel*> xdgToplevel;
    } mainWindow;


    ~WLAppCtx() noexcept
    {
        // Keeping the correct order of the resources disposal

        mainWindow.xdgToplevel.reset();
        mainWindow.xdgSurface.reset();
        mainWindow.surface.reset();
        mainWindow.surfaceWLSideBuffer2.reset();
        mainWindow.surfaceWLSideBuffer1.reset();
        mainWindow.surfaceBufferWLPool.reset();
        mainWindow.surfaceSharedBuffer.dispose();

        availableGlobalObjects.clear();
        xdgShell.reset();
        shmProvider.reset();
        compositor.reset();
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

        // ================ Step 3: binding to the wl_compositor global to be able to create surfaces =================
        MY_LOG_INFO("wl_compositor version supported by this client: ", wl_compositor_interface.version, '.');
        for (auto& [name, objInfo] : appCtx.availableGlobalObjects)
        {
            if (objInfo.interface == wl_compositor_interface.name)
            {
                appCtx.compositor = makeWLResourceWrapperChecked(
                    static_cast<wl_compositor*>(MY_LOG_WLCALL(wl_registry_bind(
                        *appCtx.registry,
                        name,
                        &wl_compositor_interface,
                        wl_compositor_interface.version
                    ))),
                    nullptr,
                    [](auto& cmps) { MY_LOG_WLCALL(wl_compositor_destroy(cmps)); cmps = nullptr; }
                );
                if (!appCtx.compositor.hasResource())
                    throw std::system_error(errno, std::system_category(), "Failed to bind to the wl_compositor");

                objInfo.bindedVersion = wl_compositor_interface.version;

                break;
            }
        }
        if (!appCtx.compositor.hasResource())
        {
            MY_LOG_ERROR("Couldn't find a wl_compositor on the Wayland server, shutting down...");
            return 4;
        }

        registryListener.addOnGlobalEventAppListener([](uint32_t name, std::string_view interface, uint32_t version, const WLAppCtx& appCtx) {
            if (interface != wl_compositor_interface.name)
                return;

            MY_LOG_WARN("A new wl_compositor object has dynamically become available ; name=", name, ", version=", version);
            // TODO: handle this case
            (void)name; (void)version; (void)appCtx;
        });
        registryListener.addOnGlobalRemoveEventAppListener([](uint32_t name, const WLAppCtx::WLGlobalObjectInfo& info, const WLAppCtx& appCtx) {
            if (info.interface != wl_compositor_interface.name)
                return;

            MY_LOG_ERROR("A wl_compositor global object with name=", name, " has been removed from the server. This case isn't supported yet.");
            // TODO: handle this case
            (void)name; (void)info; (void)appCtx;
        });
        // ============================================== END of Step 3 ===============================================

        // ============================== Step 4: creating a surface for the main window ==============================

        // The surface will be using Wayland's shared memory buffers for holding the surface pixels.
        // This feature is provided by wl_shm global object(s). So firstly we have to bind to it.
        MY_LOG_INFO("Looking up a wl_shm global object, the version supported by this client: ", wl_shm_interface.version, "...");
        for (auto& [name, objInfo] : appCtx.availableGlobalObjects)
        {
            if (objInfo.interface == wl_shm_interface.name)
            {
                MY_LOG_INFO("    ... Found a wl_shm object with name=", name, ", binding...");

                const auto versionToBind = wl_shm_interface.version;

                appCtx.shmProvider = makeWLResourceWrapperChecked(
                    static_cast<wl_shm*>(MY_LOG_WLCALL(wl_registry_bind(
                        appCtx.registry.getResource(),
                        name,
                        &wl_shm_interface,
                        versionToBind
                    ))),
                    nullptr,
                    [](auto& shm) { MY_LOG_WLCALL(wl_shm_destroy(shm)); shm = nullptr; }
                );
                if (!appCtx.shmProvider.hasResource())
                    throw std::system_error(errno, std::system_category(), "Failed to bind to the wl_shm");

                objInfo.bindedVersion = versionToBind;

                break;
            }
        }
        if (!appCtx.shmProvider.hasResource())
        {
            MY_LOG_ERROR("Couldn't find a wl_shm object on the Wayland server, shutting down...");
            return 5;
        }

        // Next, we have to allocate a shared memory buffer (POSIX's shm_* + mmap APIs).
        // We're going to write pixels into it, and then the server will read from it.
        // The space is multiplied by 2 because the double buffering technique is going to be used to avoid
        //   flickering issues.
        // appCtx.mainWindow.currentlyUsedBufferIdx will be holding the index of the buffer which is currently being
        //   read by the server.
        appCtx.mainWindow.surfaceSharedBuffer = SharedMemoryBuffer::allocate(
            appCtx.mainWindow.width * appCtx.mainWindow.bytesPerPixel * appCtx.mainWindow.height * 2
        );

        // Now, share the whole buffer with the server so it gets able to use it
        appCtx.mainWindow.surfaceBufferWLPool = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_shm_create_pool(
                *appCtx.shmProvider,
                appCtx.mainWindow.surfaceSharedBuffer.getFd(),
                appCtx.mainWindow.surfaceSharedBuffer.getSize()
            )),
            nullptr,
            [](auto& shmPool) { MY_LOG_WLCALL(wl_shm_pool_destroy(shmPool)); shmPool = nullptr; }
        );
        if (!appCtx.mainWindow.surfaceBufferWLPool.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to create a wl_shm_pool for the main window");

        // Separate the buffer into 2 Wayland sub-buffers (for the double-buffering)
        appCtx.mainWindow.surfaceWLSideBuffer1 = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_shm_pool_create_buffer(
                *appCtx.mainWindow.surfaceBufferWLPool,
                appCtx.mainWindow.getSurfaceBufferOffsetForIdx(0),
                appCtx.mainWindow.width,
                appCtx.mainWindow.height,
                appCtx.mainWindow.width * appCtx.mainWindow.bytesPerPixel,
                WL_SHM_FORMAT_XRGB8888
            )),
            nullptr,
            [](auto& buf) { MY_LOG_WLCALL(wl_buffer_destroy(buf)); buf = nullptr; }
        );
        if (!appCtx.mainWindow.surfaceWLSideBuffer1.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to create 1st wl_buffer for the main window");

        appCtx.mainWindow.surfaceWLSideBuffer2 = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_shm_pool_create_buffer(
                *appCtx.mainWindow.surfaceBufferWLPool,
                appCtx.mainWindow.getSurfaceBufferOffsetForIdx(1),
                appCtx.mainWindow.width,
                appCtx.mainWindow.height,
                appCtx.mainWindow.width * appCtx.mainWindow.bytesPerPixel,
                WL_SHM_FORMAT_XRGB8888
            )),
            nullptr,
            [](auto& buf) { MY_LOG_WLCALL(wl_buffer_destroy(buf)); buf = nullptr; }
        );
        if (!appCtx.mainWindow.surfaceWLSideBuffer2.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to create 2nd wl_buffer for the main window");

        // Creating a surface
        appCtx.mainWindow.surface = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(wl_compositor_create_surface(appCtx.compositor.getResource())),
            nullptr,
            [](auto& srf) { MY_LOG_WLCALL(wl_surface_destroy(srf)); srf = nullptr; }
        );
        if (!appCtx.mainWindow.surface.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to create a wl_surface for the main window");

        // Initially attaching the first buffer to the surface
        MY_LOG_WLCALL_VALUELESS(wl_surface_attach(*appCtx.mainWindow.surface, appCtx.mainWindow.getPendingWLSideBuffer(), 0, 0));

        // Initializing the buffer with the "silver" (#C0C0C0) color
        appCtx.mainWindow.drawVia([](std::size_t /*x*/, std::size_t /*y*/, std::byte& b, std::byte& g, std::byte& r) {
            b = g = r = std::byte{0xC0};
        });
        // Letting the server know that it should re-render the whole buffer
        MY_LOG_WLCALL_VALUELESS(wl_surface_damage_buffer(*appCtx.mainWindow.surface, 0, 0, appCtx.mainWindow.width, appCtx.mainWindow.height));
        // ============================================== END of Step 4 ===============================================

        // ============ Step 5: assigning the role to the main window surface using the XDG shell protocol ============

        // First, binding to the xdg_wm_base global object
        MY_LOG_INFO("Looking up a xdg_wm_base global object, the version supported by this client: ", xdg_wm_base_interface.version, "...");
        for (auto& [name, objInfo] : appCtx.availableGlobalObjects)
        {
            if (objInfo.interface == xdg_wm_base_interface.name)
            {
                const auto versionToBind = xdg_wm_base_interface.version;

                MY_LOG_INFO("    ... Found a xdg_wm_base object with name=", name, ", binding to version ", versionToBind, "...");

                appCtx.xdgShell = makeWLResourceWrapperChecked(
                    static_cast<xdg_wm_base*>(MY_LOG_WLCALL(wl_registry_bind(
                        appCtx.registry.getResource(),
                        name,
                        &xdg_wm_base_interface,
                        versionToBind
                    ))),
                    nullptr,
                    [](auto& xdgShell) { MY_LOG_WLCALL(xdg_wm_base_destroy(xdgShell)); xdgShell = nullptr; }
                );

                if (!appCtx.xdgShell.hasResource())
                    throw std::system_error(errno, std::system_category(), "Failed to bind to the xdg_wm_base");

                objInfo.bindedVersion = versionToBind;

                break;
            }
        }

        // Creating an xdg_surface from the main window's wl_surface
        appCtx.mainWindow.xdgSurface = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(xdg_wm_base_get_xdg_surface(*appCtx.xdgShell, *appCtx.mainWindow.surface)),
            nullptr,
            [](auto& xdgSurface) { MY_LOG_WLCALL(xdg_surface_destroy(xdgSurface)); xdgSurface = nullptr; }
        );
        if (!appCtx.mainWindow.xdgSurface.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to create an xdg_surface for the main window");

        // Assigning the toplevel role to the main window via creating a xdg_toplevel from its xdg_surface
        appCtx.mainWindow.xdgToplevel = makeWLResourceWrapperChecked(
            MY_LOG_WLCALL(xdg_surface_get_toplevel(*appCtx.mainWindow.xdgSurface)),
            nullptr,
            [](auto& xdgToplevel) { MY_LOG_WLCALL(xdg_toplevel_destroy(xdgToplevel)); xdgToplevel = nullptr; }
        );
        if (!appCtx.mainWindow.xdgToplevel.hasResource())
            throw std::system_error(errno, std::system_category(), "Failed to create an xdg_toplevel for the main window");

        // Setting the main window title
        MY_LOG_WLCALL_VALUELESS(xdg_toplevel_set_title(*appCtx.mainWindow.xdgToplevel, "WaylandInputWindow"));

        // ============================================== END of Step 5 ===============================================
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
