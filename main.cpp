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

    // The bridge to all input devices: mouses, keyboards, touchpads, touchscreens, etc.
    WLResourceWrapper<wl_seat*> inputDevicesManager;

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

    // Input devices
    struct
    {
        WLResourceWrapper<wl_keyboard*> wlDevice;
    } keyboard;

    struct
    {
        WLResourceWrapper<wl_pointer*> wlDevice;
    } pointingDev;

    struct
    {
        WLResourceWrapper<wl_touch*> wlDevice;
    } touchScreen;


    bool shouldExit = false;


    ~WLAppCtx() noexcept
    {
        // Keeping the correct order of the resources disposal

        touchScreen.wlDevice.reset();
        pointingDev.wlDevice.reset();
        keyboard.wlDevice.reset();

        mainWindow.xdgToplevel.reset();
        mainWindow.xdgSurface.reset();
        mainWindow.surface.reset();
        mainWindow.surfaceWLSideBuffer2.reset();
        mainWindow.surfaceWLSideBuffer1.reset();
        mainWindow.surfaceBufferWLPool.reset();
        mainWindow.surfaceSharedBuffer.dispose();

        availableGlobalObjects.clear();
        inputDevicesManager.reset();
        xdgShell.reset();
        shmProvider.reset();
        compositor.reset();
        registry.reset();
        connection.reset();
    }
};


struct ContentState
{
    int viewportOffsetX = 0;
    int viewportOffsetY = 0;

    // (0; +inf). 1.0 means normal zoom (100%), 0.5 - 50%, 2.0 - 200%, etc.
    double viewportZoom = 1;
    unsigned viewportZoomCenterX = 0;
    unsigned viewportZoomCenterY = 0;
};


static void renderMainWindow(WLAppCtx& appCtx, ContentState contentState);


int main(int, char*[])
{
    try
    {
        WLAppCtx appCtx;
        ContentState contentState;

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
        renderMainWindow(appCtx, contentState);
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

        // ================================ Step 6: Input: binding to a wl_seat global ================================
        MY_LOG_INFO("Looking up a wl_seat global object and binding to it, the version supported by this client: ", wl_seat_interface.version, "...");
        for (auto& [name, objInfo] : appCtx.availableGlobalObjects)
        {
            if (objInfo.interface == wl_seat_interface.name)
            {
                MY_LOG_INFO("    ... Found a wl_seat global object of version=", objInfo.version, " with name=", name);

                const auto versionToBind = std::min<uint32_t>(wl_seat_interface.version, objInfo.version);

                appCtx.inputDevicesManager = makeWLResourceWrapperChecked(
                    static_cast<wl_seat*>(MY_LOG_WLCALL(wl_registry_bind(
                        *appCtx.registry,
                        name,
                        &wl_seat_interface,
                        versionToBind
                    ))),
                    nullptr,
                    [versionToBind](auto& seat) {
                        if (versionToBind < 5)
                            MY_LOG_WLCALL_VALUELESS(wl_seat_destroy(seat));
                        else
                            MY_LOG_WLCALL_VALUELESS(wl_seat_release(seat));
                        seat = nullptr;
                    }
                );
                if (!appCtx.inputDevicesManager.hasResource())
                    throw std::system_error{errno, std::system_category(), "Failed to bind to the wl_seat using the version=" + std::to_string(versionToBind)};

                objInfo.bindedVersion = versionToBind;

                break;
            }
        }
        if (!appCtx.inputDevicesManager.hasResource())
            throw std::runtime_error{"Found no wl_seat objects"};
        // ============================================== END of Step 6 ===============================================

        // ======================== Step 7: Input: listening to the input devices availability ========================

        // We will connect to/disconnect from input devices asynchronously via this listener, because
        //   wl_seat::capabilities events are the only way to get know that an input device becomes
        //   available (attached)/unavailable (detached)
        struct InputDevicesListenerBridge final
        {
            WLAppCtx& appCtx;
            const wl_seat_listener wlHandler = { &onCapabilities, &onName };

        public:
            using KeyboardAttachedEventListener = std::function<void()>;
            using KeyboardDetachedEventListener = std::function<void()>;

            using PointingDevAttachedEventListener = std::function<void()>;
            using PointingDevDetachedEventListener = std::function<void()>;

            using TouchscreenAttachedEventListener = std::function<void()>;
            using TouchscreenDetachedEventListener = std::function<void()>;

        public:
            InputDevicesListenerBridge(WLAppCtx& appCtx) noexcept
                : appCtx(appCtx)
            {}

        public:
            void addKeyboardAttachedEventListener(KeyboardAttachedEventListener listener)
            { kbAttachedListeners_.emplace_back(std::move(listener)); }
            void addKeyboardDetachedEventListener(KeyboardDetachedEventListener listener)
            { kbDetachedListeners_.emplace_back(std::move(listener)); }

            void addPointingDevAttachedEventListener(PointingDevAttachedEventListener listener)
            { pdAttachedListeners_.emplace_back(std::move(listener)); }
            void addPointingDevDetachedEventListener(PointingDevDetachedEventListener listener)
            { pdDetachedListeners_.emplace_back(std::move(listener)); }

            void addTouchscreenAttachedEventListener(TouchscreenAttachedEventListener listener)
            { tsAttachedListeners_.emplace_back(std::move(listener)); }
            void addTouchscreenDetachedEventListener(TouchscreenDetachedEventListener listener)
            { tsDetachedListeners_.emplace_back(std::move(listener)); }

        private:
            std::vector<KeyboardAttachedEventListener> kbAttachedListeners_;
            std::vector<KeyboardDetachedEventListener> kbDetachedListeners_;
            std::vector<PointingDevAttachedEventListener> pdAttachedListeners_;
            std::vector<PointingDevDetachedEventListener> pdDetachedListeners_;
            std::vector<TouchscreenAttachedEventListener> tsAttachedListeners_;
            std::vector<TouchscreenDetachedEventListener> tsDetachedListeners_;

        private:
            static void onCapabilities(void * const selfP, wl_seat * const manager, const uint32_t capabilities)
            {
                MY_LOG_TRACE("inputDevicesListener::onCapabilities(selfP=", selfP, ", manager=", manager, ", capabilities=", capabilities, ')');

                auto& self = *static_cast<InputDevicesListenerBridge*>(selfP);

                const bool thereIsKeyboard = ( (capabilities & wl_seat_capability::WL_SEAT_CAPABILITY_KEYBOARD) != 0 );
                if ( thereIsKeyboard && !self.appCtx.keyboard.wlDevice.hasResource() )
                {
                    MY_LOG_INFO("inputDevicesListener::onCapabilities: a new keyboard device has got available.");
                    for (const auto& l : self.kbAttachedListeners_)
                        l();
                }
                else if ( !thereIsKeyboard && self.appCtx.keyboard.wlDevice.hasResource() )
                {
                    MY_LOG_INFO("inputDevicesListener::onCapabilities: the keyboard device has disappeared.");
                    for (const auto& l : self.kbDetachedListeners_)
                        l();
                }

                const bool thereIsPointingDev = ( (capabilities & wl_seat_capability::WL_SEAT_CAPABILITY_POINTER) != 0 );
                if ( thereIsPointingDev && !self.appCtx.pointingDev.wlDevice.hasResource() )
                {
                    MY_LOG_INFO("inputDevicesListener::onCapabilities: a new pointing device has got available.");
                    for (const auto& l : self.pdAttachedListeners_)
                        l();
                }
                else if ( !thereIsPointingDev && self.appCtx.pointingDev.wlDevice.hasResource() )
                {
                    MY_LOG_INFO("inputDevicesListener::onCapabilities: the pointing device has disappeared.");
                    for (const auto& l : self.pdDetachedListeners_)
                        l();
                }

                const bool thereIsTouchscreen = ( (capabilities & wl_seat_capability::WL_SEAT_CAPABILITY_TOUCH) != 0 );
                if ( thereIsTouchscreen && !self.appCtx.touchScreen.wlDevice.hasResource() )
                {
                    MY_LOG_INFO("inputDevicesListener::onCapabilities: a new touchscreen has got available.");
                    for (const auto& l : self.tsAttachedListeners_)
                        l();
                }
                else if ( !thereIsTouchscreen && self.appCtx.touchScreen.wlDevice.hasResource() )
                {
                    MY_LOG_INFO("inputDevicesListener::onCapabilities: the touchscreen has disappeared.");
                    for (const auto& l : self.tsDetachedListeners_)
                        l();
                }
            }

            static void onName(void * const /*self*/, wl_seat * const /*manager*/, const char* const /*nameUtf8*/)
            { /*These events aren't interesting for now*/ }
        } inputDevicesListener{ appCtx };

        if (const auto err = MY_LOG_WLCALL(wl_seat_add_listener(*appCtx.inputDevicesManager, &inputDevicesListener.wlHandler, &inputDevicesListener)); err != 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "Failed to set the input devices listener (wl_seat_add_listener returned " + std::to_string(err) + ")"
            );
        // ============================================== END of Step 7 ===============================================

        // ======================== Step 8: Input: handling pointing devices (mice, touchpads) ========================

        // Installing wl_pointer_listener
        struct PointingDeviceListener
        {
            WLAppCtx& appCtx;
            const wl_pointer_listener wlHandler = {
                &onEnter,
                &onLeave,
                &onMotion,
                &onButton,
                &onAxis,
                &onFrame,
                &onAxisSource,
                &onAxisStop,
                &onAxisDiscrete/*,
                // Only available since version 8
                onAxisValue120,
                // Only available since version 9
                onAxisRelativeDirection
                */
            };

        private: // wl_handler's callbacks
            // Notification that the pointer is focused on a certain surface
            static void onEnter(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t evSerial,
                wl_surface * const enteredSurface,
                const wl_fixed_t surfaceLocalX,
                const wl_fixed_t surfaceLocalY
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onEnter(selfP=", selfP, ", ",
                                                             "pd=", pd, ", ",
                                                             "evSerial=", evSerial, ", ",
                                                             "enteredSurface=", enteredSurface, ", ",
                                                             "surfaceLocalX=", surfaceLocalX, ", ",
                                                             "surfaceLocalY=", surfaceLocalY,
                                                             ')');
            }

            // Notification that the pointer is no longer focused on a certain surface
            static void onLeave(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t evSerial,
                wl_surface * const surfaceLeft
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onLeave(selfP=", selfP, ", ",
                                                             "pd=", pd, ", ",
                                                             "evSerial=", evSerial, ", ",
                                                             "surfaceLeft=", surfaceLeft,
                                                             ')');
            }

            // Notification of pointer location change
            static void onMotion(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t evTimestampMs,
                const wl_fixed_t surfaceLocalX,
                const wl_fixed_t surfaceLocalY
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onMotion(selfP=", selfP, ", ",
                                                              "pd=", pd, ", ",
                                                              "evTimestampMs=", evTimestampMs, ", ",
                                                              "surfaceLocalX=", surfaceLocalX, ", ",
                                                              "surfaceLocalY=", surfaceLocalY,
                                                              ')');
            }

            // Mouse button click and release notifications
            static void onButton(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t evSerial,
                const uint32_t evTimestampMs,
                const uint32_t button,
                const uint32_t state
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onButton(selfP=", selfP, ", ",
                                                              "pd=", pd, ", ",
                                                              "evSerial=", evSerial, ", ",
                                                              "evTimestampMs=", evTimestampMs, ", ",
                                                              "button=", button, ", ",
                                                              "state=", state,
                                                              ')');
            }

            // Scroll and other axis notifications
            static void onAxis(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t evTimestampMs,
                const uint32_t axisType,
                // For scroll events (vertical and horizontal scroll axes), it is the length of
                //   a vector along the specified axis in a coordinate space identical to those of motion events,
                //   representing a relative movement along the specified axis.
                const wl_fixed_t value
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onAxis(selfP=", selfP, ", ",
                                                            "pd=", pd, ", ",
                                                            "evTimestampMs=", evTimestampMs, ", ",
                                                            "axisType=", axisType, ", ",
                                                            "value=", value,
                                                            ')');
            }

            // Indicates the end of a set of events that logically belong together.
            // A client is expected to accumulate the data in all events within the frame before proceeding
            static void onFrame(
                void * const selfP,
                wl_pointer * const pd
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onFrame(selfP=", selfP, ", ",
                                                             "pd=", pd,
                                                             ')');
            }

            // Source information for scroll and other axes
            static void onAxisSource(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t axisSource
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onAxisSource(selfP=", selfP, ", ",
                                                                  "pd=", pd, ", ",
                                                                  "axisSource=", axisSource,
                                                                  ')');
            }

            // Stop notification for scroll and other axes.
            // For some wl_pointer.axis_source types, a wl_pointer.axis_stop event is sent to notify a client that
            //   the axis sequence has terminated.
            // This enables the client to implement kinetic scrolling.
            // See the wl_pointer.axis_source documentation for information on when this event may be generated.
            static void onAxisStop(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t evTimestampMs,
                const uint32_t axisStopped
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onAxisStop(selfP=", selfP, ", ",
                                                                "pd=", pd, ", ",
                                                                "evTimestampMs=", evTimestampMs, ", ",
                                                                "axisStopped=", axisStopped,
                                                                ')');
            }

            // Discrete step information for scroll and other axes.
            // This event carries the axis value of the wl_pointer.axis event in discrete steps
            //   (e.g. mouse wheel clicks).
            // This event is deprecated with wl_pointer version 8 - this event is not sent to clients supporting
            //   version 8 or later.
            static void onAxisDiscrete(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t axisType,
                const int32_t discreteNumberOfSteps
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onAxisDiscrete(selfP=", selfP, ", ",
                                                                    "pd=", pd, ", ",
                                                                    "axisType=", axisType, ", ",
                                                                    "discreteNumberOfSteps=", discreteNumberOfSteps,
                                                                    ')');

                return (void)onAxisValue120(selfP, pd, axisType, discreteNumberOfSteps * 120);
            }

            // Axis high-resolution scroll event
            // Discrete high-resolution scroll information.
            // This event replaces the wl_pointer.axis_discrete event in clients supporting wl_pointer version 8
            //   or later.
            static void onAxisValue120(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t axisType,
                const int32_t one120thFractionsOf1Step
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onAxisValue120(selfP=", selfP, ", ",
                                                                    "pd=", pd, ", ",
                                                                    "axisType=", axisType, ", ",
                                                                    "one120thFractionsOf1Step=", one120thFractionsOf1Step,
                                                                    ')');
            }

            // Relative directional information of the entity causing the axis motion.
            // For a wl_pointer.axis event, the wl_pointer.axis_relative_direction event specifies
            //   the movement direction of the entity causing the wl_pointer.axis event.
            // For example:
            //   - if a user's fingers on a touchpad move down and this causes a wl_pointer.axis vertical_scroll down
            //     event, the physical direction is 'identical'
            //   - if a user's fingers on a touchpad move down and this causes a wl_pointer.axis vertical_scroll up
            //     event ('natural scrolling'), the physical direction is 'inverted'.
            // A client may use this information to adjust scroll motion of components.
            // Specifically, enabling natural scrolling causes the content to change direction compared to traditional
            //   scrolling.
            // Some widgets like volume control sliders should usually match the physical direction regardless of
            //   whether natural scrolling is active.
            // This event enables clients to match the scroll direction of a widget to the physical direction.
            static void onAxisRelativeDirection(
                void * const selfP,
                wl_pointer * const pd,
                const uint32_t axisType,
                const uint32_t relativeDirectionType
            );
        } pdListener{ appCtx };

        inputDevicesListener.addPointingDevAttachedEventListener([&appCtx, &pdListener] {
            appCtx.pointingDev.wlDevice = makeWLResourceWrapperChecked(
                MY_LOG_WLCALL(wl_seat_get_pointer(*appCtx.inputDevicesManager)),
                nullptr,
                [](auto& pd) { MY_LOG_WLCALL_VALUELESS(wl_pointer_release(pd)); pd = nullptr; }
            );
            if (!appCtx.pointingDev.wlDevice.hasResource())
                throw std::system_error{errno, std::system_category(), "Failed to obtain a pointing device (wl_pointer), although it had been available"};

            if (const auto err = MY_LOG_WLCALL(wl_pointer_add_listener(*appCtx.pointingDev.wlDevice, &pdListener.wlHandler, &pdListener)); err != 0)
                throw std::system_error{
                    errno,
                    std::system_category(),
                    "Failed to set the pointing device listener (wl_pointer_add_listener returned " + std::to_string(err) + ")"
                };
        });
        // ============================================== END of Step 8 ===============================================
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


void renderMainWindow(WLAppCtx& appCtx, ContentState contentState)
{
    //if (contentState.contentZoom <= 0)
    //    throw std::range_error{ "contentState.contentZoom <= 0" };

    // Rendering the chess board pattern respecting the content's offsets and zoom

    constexpr auto cellSideBasicSize = 60 /*px*/;

    const auto sideZoom = std::sqrt(contentState.viewportZoom);

    appCtx.mainWindow.drawVia([cs = contentState, zoom = sideZoom](std::size_t x, std::size_t y, std::byte& b, std::byte& g, std::byte& r) {
        const auto xRelZoomCenter = (x > cs.viewportZoomCenterX)
                                    ? static_cast<int>(x - cs.viewportZoomCenterX)
                                    : -static_cast<int>(cs.viewportZoomCenterX - x);
        const auto srcXRelZoomCenter = static_cast< std::remove_cv_t<decltype(xRelZoomCenter)> >(std::round(xRelZoomCenter / zoom));
        const auto srcXGlobal = static_cast<std::int64_t>(cs.viewportOffsetX) +
                                static_cast<std::int64_t>(cs.viewportZoomCenterX) +
                                static_cast<std::int64_t>(srcXRelZoomCenter);

        const auto yRelZoomCenter = (y > cs.viewportZoomCenterY)
                                    ? static_cast<int>(y - cs.viewportZoomCenterY)
                                    : -static_cast<int>(cs.viewportZoomCenterY - y);
        const auto srcYRelZoomCenter = static_cast< std::remove_cv_t<decltype(yRelZoomCenter)> >(std::round(yRelZoomCenter / zoom));
        const auto srcYGlobal = static_cast<std::int64_t>(cs.viewportOffsetY) +
                                static_cast<std::int64_t>(cs.viewportZoomCenterY) +
                                static_cast<std::int64_t>(srcYRelZoomCenter);

        const auto srcCellColumn = (srcXGlobal > 0)
                                   ? (srcXGlobal / cellSideBasicSize)
                                   : -(1 + (-srcXGlobal / cellSideBasicSize));
        const auto srcRowColumn = (srcYGlobal > 0)
                                   ? (srcYGlobal / cellSideBasicSize)
                                   : -(1 + (-srcYGlobal / cellSideBasicSize));

        const bool columnIsEven = ( (srcCellColumn % 2) == 0 );
        const bool rowIsEven    = ( (srcRowColumn % 2) == 0 );

        if (columnIsEven == rowIsEven)
            r = g = b = std::byte{0x00}; // pure black
        else
            r = g = b = std::byte{0xC0}; // silver
    });
}

