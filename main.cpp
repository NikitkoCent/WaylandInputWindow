#include "utilities.h"               // WLResourceWrapper, makeWLResourceWrapperChecked, logging::*, MY_LOG_*
#include <wayland-client.h>          // wl_*
#include <xdg-shell.h>               // xdg_*
#include <linux/input-event-codes.h> // BTN_*
#include <xkbcommon/xkbcommon.h>     // xkb_*
#include <sys/mman.h>                // mmap, munmap
#include <cstdint>                   // std::int*_t, std::uint*_t
#include <string>                    // std::string
#include <string_view>               // std::string_view
#include <unordered_map>             // std::unordered_map
#include <optional>                  // std::optional
#include <variant>                   // std::variant
#include <vector>                    // std::vector
#include <utility>                   // std::move
#include <limits>                    // std::numeric_limits
#include <bitset>                    // std::bitset
#include <memory>                    // std::shared_ptr


namespace wl_pointer_event_frame_types
{
    struct Enter final
    {
        std::uint32_t evSerial = 0;
        wl_surface* surfaceEntered = nullptr;
        double posX = 0;
        double posY = 0;
    };

    struct Leave final
    {
        std::uint32_t evSerial = 0;
        wl_surface* surfaceLeft = nullptr;
    };

    struct Motion final
    {
        std::uint32_t evTimestampMs;
        double surfaceLocalX;
        double surfaceLocalY;
    };

    struct Button final
    {
        std::uint32_t evSerial;
        std::uint32_t evTimestampMs;
        std::uint32_t button;
        wl_pointer_button_state state;
    };

    struct Axes final
    {
        struct Axis final
        {
            // wl_pointer::axis

            std::optional<std::uint32_t> launchedTimestampMs;
            // For scroll events (vertical and horizontal scroll axes), it is the length of
            //   a vector along the specified axis in a coordinate space identical to those of motion events,
            //   representing a relative movement along the specified axis.
            std::optional<double> value;

            // wl_pointer::axis_stop
            std::optional<std::uint32_t> stoppedTimestampMs;

            // wl_pointer::axis_discrete / wl_pointer::axis_value120 (since ver.5 / 8)
            std::optional<std::int32_t> one120thFractionsOfWheelStep;

            // wl_pointer::axis_relative_direction (since ver.9)
            std::optional<std::uint32_t> relativeDirectionType;
        };

        std::optional<Axis> horizontal;
        std::optional<Axis> vertical;

        // wl_pointer::axis_source
        std::optional<wl_pointer_axis_source> axisSource;
    };
}


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

        // Indicates explicit requests for re-rendering
        bool mustBeRedrawn = false;
        // The rendering requests are delayed until the flag is false
        bool readyToBeRedrawn = false;
    } mainWindow;

    // Input devices
    struct
    {
        WLResourceWrapper<wl_keyboard*> wlDevice;

        // Currently the libxkbcommon is the only supported way to work with keyboard input
        struct
        {
            std::shared_ptr<xkb_context> context;
            std::shared_ptr<xkb_keymap> keymap;
            std::shared_ptr<xkb_state> state;
        } xkb;

        struct
        { // TODO: implement key repeating using this info
            /// the rate of repeating keys in characters per second. Zero should disable any repeating
            uint32_t rate = 0;
            /// delay in milliseconds since key down until repeating starts
            uint32_t delay = 0;
        } repeatInfo;

        uint32_t lastSerial = 0;
    } keyboard;

    struct PointingDevice
    {
        WLResourceWrapper<wl_pointer*> wlDevice;


        struct PositionOnSurface
        {
            double x = 0;
            double y = 0;
        };
        // The empty optional means the pointer isn't over the surface
        std::optional<PositionOnSurface> positionOnMainWindowSurface;

        // buttonsPressedState[i] is true if ith button is pressed
        std::bitset<32> buttonsPressedState;
        enum : std::uint8_t
        {
            IDX_LMB = 0,
            IDX_RMB = 1,
            // usually the wheel
            IDX_MMB = 2,
            IDX_OTHERS_BEGIN
        };


        // Holds the accumulated data of all wl_pointer events until a wl_pointer::frame is received
        struct EventFrame
        {
            wl_pointer* sourceDev;

            std::variant<
                wl_pointer_event_frame_types::Enter,
                wl_pointer_event_frame_types::Leave,
                wl_pointer_event_frame_types::Motion,
                wl_pointer_event_frame_types::Button,
                wl_pointer_event_frame_types::Axes
            > info;
        };
        std::optional<EventFrame> eventFrame;
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
        keyboard.xkb.state.reset();
        keyboard.xkb.keymap.reset();
        keyboard.xkb.context.reset();

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
    double viewportOffsetX = 0;
    double viewportOffsetY = 0;

    // (0; +inf). 1.0 means normal zoom (100%), 0.5 - 50%, 2.0 - 200%, etc.
    double viewportZoom = 1;
    // The point is in the viewport local coordinate system, so it must be within the range [0; width)
    double viewportZoomCenterLocalX = 0;
    // The point is in the viewport local coordinate system, so it must be within the range [0; height)
    double viewportZoomCenterLocalY = 0;

public:
    static constexpr double ZOOM_FACTOR = 1.25;

public: // modifiers
    ContentState movedFor(double offsetX, double offsetY) const;

    ContentState zoomedIn(double zoomFactor = ZOOM_FACTOR) const;
    ContentState zoomedIn(unsigned newZoomCenterX, unsigned newZoomCenterY, double zoomFactor = ZOOM_FACTOR) const;

    ContentState zoomedOut(double zoomFactor = ZOOM_FACTOR) const;
    ContentState zoomedOut(unsigned newZoomCenterX, unsigned newZoomCenterY, double zoomFactor = ZOOM_FACTOR) const;

    ContentState restoredZoom() const;
};

bool operator==(const ContentState& lhs, const ContentState& rhs) noexcept;
bool operator!=(const ContentState& lhs, const ContentState& rhs) noexcept;


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

        // Will render the content right before the event loop below
        appCtx.mainWindow.mustBeRedrawn = true;
        appCtx.mainWindow.readyToBeRedrawn = true;
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
            ContentState& contentState;

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
            // Indicates the end of a set of events that logically belong together.
            // A client is expected to accumulate the data in all events within the frame before proceeding
            static void onFrame(
                void * const selfP,
                wl_pointer * const pd
            ) {
                MY_LOG_TRACE("PointingDeviceListener::onFrame(selfP=", selfP, ", ",
                                                             "pd=", pd,
                                                             ')');

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                if (!self.appCtx.pointingDev.eventFrame.has_value())
                {
                    MY_LOG_WARN("An empty wl_pointer frame to handle. Discarding.");
                    return;
                }

                auto evFrame = std::move(*self.appCtx.pointingDev.eventFrame);
                self.appCtx.pointingDev.eventFrame.reset();

                if (pd != evFrame.sourceDev)
                {
                    MY_LOG_ERROR("The wl_pointer=", pd, " of the wl_pointer::frame event doesn't correspond to the wl_pointer=", evFrame.sourceDev, " initialized the frame. Discarding the frame.");
                    return;
                }
                if (evFrame.sourceDev != self.appCtx.pointingDev.wlDevice)
                {
                    auto * const wlDevice = self.appCtx.pointingDev.wlDevice.hasResource() ? &*self.appCtx.pointingDev.wlDevice : nullptr;
                    MY_LOG_WARN("The wl_pointer=", evFrame.sourceDev, " of the wl_pointer frame isn't the current wl_pointer=", wlDevice, ". Discarding the frame.");
                    return;
                }

                std::visit([&self](auto& frame) {
                    self.handleFrame(std::move(frame));
                }, evFrame.info);
            }

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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                if (self.appCtx.pointingDev.eventFrame.has_value())
                {
                    MY_LOG_ERROR("The wl_pointer frame already contains an event. Skipping this wl_pointer::enter.");
                    return;
                }

                const auto xD = wl_fixed_to_double(surfaceLocalX);
                const auto yD = wl_fixed_to_double(surfaceLocalY);

                self.appCtx.pointingDev.eventFrame.emplace(
                    WLAppCtx::PointingDevice::EventFrame{
                        pd,
                        wl_pointer_event_frame_types::Enter{
                            evSerial, enteredSurface, xD, yD
                        }
                    }
                );
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                if (self.appCtx.pointingDev.eventFrame.has_value())
                {
                    MY_LOG_ERROR("The wl_pointer frame already contains an event. Skipping this wl_pointer::leave.");
                    return;
                }

                self.appCtx.pointingDev.eventFrame.emplace(
                    WLAppCtx::PointingDevice::EventFrame{
                        pd,
                        wl_pointer_event_frame_types::Leave{
                            evSerial, surfaceLeft
                        }
                    }
                );
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                if (self.appCtx.pointingDev.eventFrame.has_value())
                {
                    MY_LOG_ERROR("The wl_pointer frame already contains an event. Skipping this wl_pointer::motion.");
                    return;
                }

                const auto xD = wl_fixed_to_double(surfaceLocalX);
                const auto yD = wl_fixed_to_double(surfaceLocalY);

                self.appCtx.pointingDev.eventFrame.emplace(
                    WLAppCtx::PointingDevice::EventFrame{
                        pd,
                        wl_pointer_event_frame_types::Motion{
                            evTimestampMs, xD, yD
                        }
                    }
                );
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                if (self.appCtx.pointingDev.eventFrame.has_value())
                {
                    MY_LOG_ERROR("The wl_pointer frame already contains an event. Skipping this wl_pointer::button.");
                    return;
                }

                self.appCtx.pointingDev.eventFrame.emplace(
                    WLAppCtx::PointingDevice::EventFrame{
                        pd,
                        wl_pointer_event_frame_types::Button{
                            evSerial, evTimestampMs, button, static_cast<wl_pointer_button_state>(state)
                        }
                    }
                );
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                auto* const axesFrameP = self.getAxesFramePtrOrNull(pd, "wl_pointer::axis");
                if (axesFrameP == nullptr) return;
                auto& axesFrame = *axesFrameP;

                auto& axisToHandle = (axisType == wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL)
                                                       ? axesFrame.vertical
                                                       : axesFrame.horizontal;

                if (!axisToHandle.has_value())
                    axisToHandle.emplace(wl_pointer_event_frame_types::Axes::Axis{});
                else if (axisToHandle->value.has_value())
                {
                    MY_LOG_ERROR(
                        "The wl_pointer frame already contains a ",
                        (axisType == wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vertical" : "horizontal"
                        " axis event. Skipping this one."
                    );
                    return;
                }

                axisToHandle->launchedTimestampMs = evTimestampMs;
                axisToHandle->value = wl_fixed_to_double(value);
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                auto* const axesFrameP = self.getAxesFramePtrOrNull(pd, "wl_pointer::axis_source");
                if (axesFrameP == nullptr) return;
                auto& axesFrame = *axesFrameP;

                if (axesFrame.axisSource.has_value())
                {
                    MY_LOG_ERROR("The wl_pointer frame already contains an wl_pointer::axis_source event. Skipping this one.");
                    return;
                }
                axesFrame.axisSource = static_cast<wl_pointer_axis_source>(axisSource);
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                auto* const axesFrameP = self.getAxesFramePtrOrNull(pd, "wl_pointer::axis_stop");
                if (axesFrameP == nullptr) return;
                auto& axesFrame = *axesFrameP;

                auto& axisToHandle = (axisStopped == wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL)
                                                       ? axesFrame.vertical
                                                       : axesFrame.horizontal;

                if (!axisToHandle.has_value())
                    axisToHandle.emplace(wl_pointer_event_frame_types::Axes::Axis{});
                else if (axisToHandle->stoppedTimestampMs.has_value())
                {
                    MY_LOG_ERROR(
                        "The wl_pointer frame already contains a ",
                        (axisStopped == wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vertical" : "horizontal"
                        " axis_stop event. Skipping this one."
                    );
                    return;
                }

                axisToHandle->stoppedTimestampMs = evTimestampMs;
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

                auto& self = *static_cast<PointingDeviceListener*>(selfP);

                auto* const axesFrameP = self.getAxesFramePtrOrNull(pd, "wl_pointer::axis_discrete / axis_value120");
                if (axesFrameP == nullptr) return;
                auto& axesFrame = *axesFrameP;

                auto& axisToHandle = (axisType == wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL)
                                                       ? axesFrame.vertical
                                                       : axesFrame.horizontal;

                if (!axisToHandle.has_value())
                    axisToHandle.emplace(wl_pointer_event_frame_types::Axes::Axis{});
                else if (axisToHandle->one120thFractionsOfWheelStep.has_value())
                {
                    MY_LOG_ERROR(
                        "The wl_pointer frame already contains a ",
                        (axisType == wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vertical" : "horizontal"
                        " axis_discrete/axis_value120 event. Skipping this one."
                    );
                    return;
                }

                axisToHandle->one120thFractionsOfWheelStep = one120thFractionsOf1Step;
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

        private: // onFrame helpers
            void handleFrame(wl_pointer_event_frame_types::Enter enterFrame) const
            {
                MY_LOG_INFO(
                    "Handling wl_pointer::enter EVENT frame:\n",
                    "  serial        = ", enterFrame.evSerial, '\n',
                    "  x             = ", enterFrame.posX, '\n',
                    "  y             = ", enterFrame.posY, '\n',
                    "  surface       = ", enterFrame.surfaceEntered
                );

                if (enterFrame.surfaceEntered != appCtx.mainWindow.surface)
                {
                    MY_LOG_WARN("wl_pointer::enter: the entered surface isn't the main window. Skipping the event frame.")
                    return;
                }

                appCtx.pointingDev.positionOnMainWindowSurface = WLAppCtx::PointingDevice::PositionOnSurface{
                    enterFrame.posX,
                    enterFrame.posY
                };
                appCtx.pointingDev.buttonsPressedState.reset();
            }

            void handleFrame(wl_pointer_event_frame_types::Leave leaveFrame) const
            {
                MY_LOG_INFO(
                    "Handling wl_pointer::leave EVENT frame:\n",
                    "  serial        = ", leaveFrame.evSerial, '\n',
                    "  surface       = ", leaveFrame.surfaceLeft
                );

                if (leaveFrame.surfaceLeft != appCtx.mainWindow.surface)
                {
                    MY_LOG_WARN("wl_pointer::leave: the surface left isn't the main window. Skipping the event frame.")
                    return;
                }

                appCtx.pointingDev.buttonsPressedState.reset();
                appCtx.pointingDev.positionOnMainWindowSurface.reset();
            }

            void handleFrame(wl_pointer_event_frame_types::Motion motionFrame) const
            {
                MY_LOG_INFO(
                    "Handling wl_pointer::motion EVENT frame:\n",
                    "  x             = ", motionFrame.surfaceLocalX, '\n',
                    "  y             = ", motionFrame.surfaceLocalY, '\n',
                    "  timestamp     = ", motionFrame.evTimestampMs, " (ms)"
                );

                if (
                    appCtx.pointingDev.positionOnMainWindowSurface.has_value() &&
                    // only LMB is pressed
                    appCtx.pointingDev.buttonsPressedState.test(WLAppCtx::PointingDevice::IDX_LMB) &&
                    (appCtx.pointingDev.buttonsPressedState.count() == 1)
                   )
                {
                    const auto [currentX, currentY] = *appCtx.pointingDev.positionOnMainWindowSurface;

                    MY_LOG_INFO("wl_pointer::motion: DRAG for x:", currentX, "->", motionFrame.surfaceLocalX, " ; ",
                                                             "y:", currentY, "->", motionFrame.surfaceLocalY);

                    const auto movingOffsetX = motionFrame.surfaceLocalX - currentX;
                    const auto movingOffsetY = motionFrame.surfaceLocalY - currentY;

                    if ((movingOffsetX != 0) || (movingOffsetY != 0))
                    {
                        // Subtracting is intended for the natural dragging effect
                        contentState = contentState.movedFor(-movingOffsetX, -movingOffsetY);
                        appCtx.mainWindow.mustBeRedrawn = true;
                    }
                }

                appCtx.pointingDev.positionOnMainWindowSurface = WLAppCtx::PointingDevice::PositionOnSurface{
                    motionFrame.surfaceLocalX,
                    motionFrame.surfaceLocalY
                };
            }

            void handleFrame(wl_pointer_event_frame_types::Button buttonFrame) const
            {
                const int buttonIdx = [linuxButton = buttonFrame.button]() -> int {
                    switch (linuxButton)
                    {
                        // BTN_MOUSE
                        case BTN_LEFT:      return WLAppCtx::PointingDevice::IDX_LMB;
                        case BTN_RIGHT:     return WLAppCtx::PointingDevice::IDX_RMB;
                        case BTN_MIDDLE:    return WLAppCtx::PointingDevice::IDX_MMB;
                        case BTN_SIDE:      return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 0;
                        case BTN_EXTRA:     return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 1;
                        case BTN_FORWARD:   return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 2;
                        case BTN_BACK:      return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 3;
                        case BTN_TASK:      return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 4;

                        // BTN_MISC
                        case BTN_0:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 5;
                        case BTN_1:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 6;
                        case BTN_2:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 7;
                        case BTN_3:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 8;
                        case BTN_4:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 9;
                        case BTN_5:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 10;
                        case BTN_6:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 11;
                        case BTN_7:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 12;
                        case BTN_8:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 13;
                        case BTN_9:         return WLAppCtx::PointingDevice::IDX_OTHERS_BEGIN + 14;
                    }
                    return -1;
                }();

                MY_LOG_INFO(
                    "Handling wl_pointer::button EVENT frame:\n",
                    "  state         = ", (buttonFrame.state == wl_pointer_button_state::WL_POINTER_BUTTON_STATE_PRESSED) ? "pressed" : "released", '\n',
                    "  button        = ", buttonFrame.button, '\n',
                    "  buttonIdx     = ", buttonIdx, '\n',
                    "  timestamp     = ", buttonFrame.evTimestampMs, " (ms)"
                );

                if ( (buttonIdx < 0) || (static_cast<unsigned>(buttonIdx) >= appCtx.pointingDev.buttonsPressedState.size()) )
                {
                    MY_LOG_WARN("wl_pointer::button: an unsupported button #", buttonFrame.button, " has been pressed or released. Skipping the event frame.");
                    return;
                }

                appCtx.pointingDev.buttonsPressedState[buttonIdx] = (buttonFrame.state == wl_pointer_button_state::WL_POINTER_BUTTON_STATE_PRESSED);
                MY_LOG_INFO(
                    "wl_pointer::button:\n"
                    "  buttons state = ", appCtx.pointingDev.buttonsPressedState
                );
            }

            void handleFrame(wl_pointer_event_frame_types::Axes axesFrame) const
            {
                MY_LOG_INFO(
                    "Handling wl_pointer::axis EVENT frame:\n",
                    "  vaxis         = ", axesFrame.vertical.has_value() ? axesFrame.vertical.value().value.value_or(0.0) : 0.0, '\n',
                    "  haxis         = ", axesFrame.horizontal.has_value() ? axesFrame.horizontal.value().value.value_or(0.0) : 0.0
                )

                double movingOffsetX = 0;
                double movingOffsetY = 0;

                if (axesFrame.horizontal.has_value())
                    movingOffsetX = axesFrame.horizontal->value.value_or(0);

                if (axesFrame.vertical.has_value())
                    movingOffsetY = axesFrame.vertical->value.value_or(0);

                if ((movingOffsetX != 0) || (movingOffsetY != 0))
                {
                    contentState = contentState.movedFor(movingOffsetX, movingOffsetY);
                    appCtx.mainWindow.mustBeRedrawn = true;
                }
            }

        private: // onAxis... helpers
            wl_pointer_event_frame_types::Axes* getAxesFramePtrOrNull(wl_pointer * const pd, std::string_view eventName) const
            {
                wl_pointer_event_frame_types::Axes* result = nullptr;

                if (appCtx.pointingDev.eventFrame.has_value())
                    result = std::get_if<wl_pointer_event_frame_types::Axes>(&appCtx.pointingDev.eventFrame->info);
                else
                    result = std::get_if<wl_pointer_event_frame_types::Axes>(
                        &appCtx.pointingDev.eventFrame.emplace(
                            WLAppCtx::PointingDevice::EventFrame{
                                pd,
                                wl_pointer_event_frame_types::Axes{}
                            }
                        ).info
                    );

                if (result == nullptr)
                    MY_LOG_ERROR("The wl_pointer frame already contains a non axis-like event. Skipping this ", eventName, ".");

                return result;
            }
        } pdListener{ appCtx, contentState };

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

        // =========================== Step 9: handling keyboard input (excl. input methods) ==========================
        { // initializing appCtx.keyboard.xkb.context
            auto* xkbContext = MY_LOG_WLCALL(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
            if (xkbContext == nullptr)
                throw std::system_error{errno, std::generic_category(), "Failed to create an xkb_context"};
            appCtx.keyboard.xkb.context.reset(
                xkbContext,
                [](auto& xkbContext) { MY_LOG_WLCALL_VALUELESS(xkb_context_unref(xkbContext)); xkbContext = nullptr; }
            );
        }

        struct KeyboardListener
        {
            WLAppCtx& appCtx;

            const wl_keyboard_listener wlHandler = {
                &onKeymap,
                &onEnter,
                &onLeave,
                &onKey,
                &onModifiers,
                &onRepeatInfo
            };

        private: // wlHandler's callbacks
            static void onKeymap(
                void * const selfP,
                wl_keyboard * const kb,
                const uint32_t format,
                const int32_t fd,
                const uint32_t sizeBytes
            ) {
                MY_LOG_TRACE("kbListener::onKeymap(selfP=", selfP, ", kb=", kb, ", format=", format, ", fd=", fd, ", size=", sizeBytes, ").");

                auto& self = *static_cast<KeyboardListener*>(selfP);

                // wl_keyboard::keymap informs how hardware-dependent keyboard scancodes translate to virtual key codes and
                //     which characters should be produced (if any).
                // The keymap info isn't sent directly, but transferred through the file descriptor `fd`.

                if (self.appCtx.keyboard.xkb.context == nullptr)
                    throw std::logic_error{"wl_keyboard::keymap: xkb_context is nullptr"};

                self.appCtx.keyboard.xkb.state.reset();
                self.appCtx.keyboard.xkb.keymap.reset();

                if (format != wl_keyboard_keymap_format::WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
                {
                    MY_LOG_ERROR("wl_keyboard::keymap: unsupported keymap format: ", format);
                    return;
                }

                char* keymapData = static_cast<char*>(mmap(nullptr, sizeBytes, PROT_READ, MAP_PRIVATE, fd, 0));
                if (keymapData == MAP_FAILED)
                {
                    MY_LOG_ERROR("wl_keyboard::keymap: mmap failed (errno=", errno, ").");
                    return;
                }

                // Creating new xkb_keymap

                // WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 means that the `fd` contains a libxkbcommon compatible, null-terminated string
                auto* const newKeymap = xkb_keymap_new_from_string(
                    self.appCtx.keyboard.xkb.context.get(),
                    keymapData,
                    XKB_KEYMAP_FORMAT_TEXT_V1,
                    XKB_KEYMAP_COMPILE_NO_FLAGS
                );

                if (const auto err = munmap(keymapData, sizeBytes); err != 0)
                    MY_LOG_WARN("wl_keyboard::keymap: munmap failed (returned ", err, " , errno=", errno, ").");
                keymapData = nullptr;

                if (newKeymap == nullptr)
                {
                    MY_LOG_ERROR("wl_keyboard::keymap: failed to create a new xkb_keymap.");
                    return;
                }
                self.appCtx.keyboard.xkb.keymap.reset(
                    newKeymap,
                    [](auto& keymap) { MY_LOG_WLCALL_VALUELESS(xkb_keymap_unref(keymap)); keymap = nullptr; }
                );

                // Creating new xkb_state

                auto* const newState = xkb_state_new(newKeymap);
                if (newState == nullptr)
                {
                    MY_LOG_ERROR("wl_keyboard::keymap: failed to create a new xkb_state");
                    return;
                }
                self.appCtx.keyboard.xkb.state.reset(
                    newState,
                    [](auto& state) { MY_LOG_WLCALL_VALUELESS(xkb_state_unref(state)); state = nullptr; }
                );
            }

            static void onEnter(
                void * const selfP,
                wl_keyboard * const kb,
                const uint32_t serial,
                wl_surface * const surface,
                wl_array * const keys
            ) {
                MY_LOG_TRACE("kbListener::onEnter(selfP=", selfP, ", kb=", kb, ", serial=", serial, ", surface=", surface, ", keys=", keys, ").");

                auto& self = *static_cast<KeyboardListener*>(selfP);
                if (self.appCtx.mainWindow.surface != surface)
                {
                    MY_LOG_WARN("wl_keyboard::enter: unexpected wl_surface=", surface, " != ", self.appCtx.mainWindow.surface.getResource(), ". Skipped.");
                    return;
                }
                self.appCtx.keyboard.lastSerial = serial;
            }

            static void onLeave(
                void * const selfP,
                wl_keyboard * const kb,
                const uint32_t serial,
                wl_surface * const surface
            ) {
                MY_LOG_TRACE("kbListener::onLeave(selfP=", selfP, ", kb=", kb, ", serial=", serial, ", surface=", surface, ").");

                auto& self = *static_cast<KeyboardListener*>(selfP);
                if (self.appCtx.mainWindow.surface != surface)
                {
                    MY_LOG_WARN("wl_keyboard::enter: unexpected wl_surface=", surface, " != ", self.appCtx.mainWindow.surface.getResource(), ". Skipped.");
                    return;
                }
                self.appCtx.keyboard.lastSerial = serial;
            }

            static void onKey(
                void * const selfP,
                wl_keyboard * const kb,
                const uint32_t serial,
                const uint32_t time,
                const uint32_t keyScancode,
                const uint32_t state
            ) {
                MY_LOG_TRACE("kbListener::onKey(selfP=", selfP, ", kb=", kb, ", serial=", serial, ", time=", time, ", keyScancode=", keyScancode, ", state=", state, ").");

                auto& self = *static_cast<KeyboardListener*>(selfP);
                self.appCtx.keyboard.lastSerial = serial;

                // "to determine the xkb keycode, clients must add 8 to the key event keycode"
                const xkb_keycode_t xkbKeycode = keyScancode + 8;

                // TODO: log verbose information about key events
                // TODO: modify the ContentState in response to key events
                // TODO: make the app shut down in response to a key press

                if (state == wl_keyboard_key_state::WL_KEYBOARD_KEY_STATE_PRESSED)
                {
                    const xkb_keysym_t xkbKeysym = MY_LOG_WLCALL(xkb_state_key_get_one_sym(self.appCtx.keyboard.xkb.state.get(), xkbKeycode));

                    MY_LOG_INFO("wl_keyboard::key: key pressed (XKB keycode=", xkbKeycode, " , XKB keysym=", xkbKeysym, ").");
                }
                else if (state == wl_keyboard_key_state::WL_KEYBOARD_KEY_STATE_RELEASED)
                {
                }
                else
                {
                    MY_LOG_ERROR("wl_keyboard::key: unknown key state=", state);
                    return;
                }
            }

            static void onModifiers(
                void * const selfP,
                wl_keyboard * const kb,
                const uint32_t serial,
                const uint32_t modsDepressed,
                const uint32_t modsLatched,
                const uint32_t modsLocked,
                const uint32_t group
            ) {
                MY_LOG_TRACE("kbListener::onModifiers(selfP=", selfP, ", kb=", kb, ", serial=", serial, ", modsDepressed=", modsDepressed, ", modsLatched=", modsLatched, ", modsLocked=", modsLocked, ", group=", group, ").");

                auto& self = *static_cast<KeyboardListener*>(selfP);
                if (self.appCtx.keyboard.wlDevice != kb)
                {
                    MY_LOG_WARN("wl_keyboard::modifiers: unexpected wl_keyboard=", kb, " != ", self.appCtx.keyboard.wlDevice.getResource(), ". Skipped.");
                    return;
                }
                self.appCtx.keyboard.lastSerial = serial;

                const xkb_state_component state = MY_LOG_WLCALL(xkb_state_update_mask(
                    self.appCtx.keyboard.xkb.state.get(),
                    modsDepressed,
                    modsLatched,
                    modsLocked,
                    0,
                    0,
                    group
                ));
                (void)state;

                // TODO: log information about pressed/release modifiers and when the current keyboard layout is changed
            }

            static void onRepeatInfo(
                [[maybe_unused]] void * const selfP,
                [[maybe_unused]] wl_keyboard * const kb,
                [[maybe_unused]] const int32_t rate,
                [[maybe_unused]] const int32_t delay
            ) {
                MY_LOG_TRACE("kbListener::onRepeatInfo(selfP=", selfP, ", kb=", kb, ", rate=", rate, ", delay=", delay, ").");

                auto& self = *static_cast<KeyboardListener*>(selfP);
                if (self.appCtx.keyboard.wlDevice != kb)
                {
                    MY_LOG_WARN("wl_keyboard::repeat_info: unexpected wl_keyboard=", kb, " != ", self.appCtx.keyboard.wlDevice.getResource(), ". Skipped.");
                    return;
                }

                self.appCtx.keyboard.repeatInfo.rate = rate;
                self.appCtx.keyboard.repeatInfo.delay = delay;
            }
        } kbListener{ appCtx };

        inputDevicesListener.addKeyboardAttachedEventListener([&appCtx, &kbListener] {
            // TODO: handle the cases when a keyboard is dynamically attached/detached

            appCtx.keyboard.wlDevice = makeWLResourceWrapperChecked(
                MY_LOG_WLCALL(wl_seat_get_keyboard(*appCtx.inputDevicesManager)),
                nullptr,
                [](auto& kb) { MY_LOG_WLCALL_VALUELESS(wl_keyboard_release(kb)); kb = nullptr; }
            );
            if (!appCtx.keyboard.wlDevice.hasResource())
                throw std::system_error{errno, std::system_category(), "Failed to obtain a keyboard (wl_keyboard), although it had been available"};

            if (const auto err = MY_LOG_WLCALL(wl_keyboard_add_listener(*appCtx.keyboard.wlDevice, &kbListener.wlHandler, &kbListener)); err != 0)
                throw std::system_error{
                    errno,
                    std::system_category(),
                    "Failed to set the keyboard listener (wl_keyboard_add_listener returned " + std::to_string(err) + ")"
                };

            appCtx.keyboard.repeatInfo.rate = 0;
            appCtx.keyboard.repeatInfo.delay = 0;
        });
        // ============================================== END of Step 9 ===============================================

        // === Step N: install listeners to all the Wayland objects (to handle errors and other important messages) ===

        // 1. wl_display
        struct ConnectionListener
        {
            WLAppCtx& appCtx;
            const wl_display_listener wlHandler = { &onError, &onDeleteId };

            static void onError(void* self, wl_display* /*connection*/, void* object_id, uint32_t code, const char* message)
            {
                MY_LOG_ERROR("A fatal wl_display error occurred: object_id=", object_id, " , code=", code, " , message=\"", message, "\"");

                static_cast<ConnectionListener*>(self)->appCtx.shouldExit = true;
            }

            static void onDeleteId(void* /*self*/, wl_display* /*connection*/, uint32_t /*id*/) {}
        } connectionListener{ appCtx } ;
        if (const auto err = MY_LOG_WLCALL(wl_display_add_listener(*appCtx.connection, &connectionListener.wlHandler, &connectionListener)); err != 0)
        {
            const std::error_code sysErr{errno, std::system_category()};
            MY_LOG_ERROR("Failed to set the connection listener: wl_display_add_listener returned ", err, " (errno=", sysErr, " \"", sysErr.message(), "\").");
        }

        // 2. wl_compositor doesn't have events yet

        // 3. wl_shm only emits wl_shm_format events currently, which aren't interesting at the moment

        // 4. xdg_wm_base
        struct XdgShellListener
        {
            WLAppCtx& appCtx;
            const xdg_wm_base_listener wlHandler = { &onPing };

            static void onPing(void * const self, xdg_wm_base * const xdgShell, const uint32_t serial)
            {
                MY_LOG_TRACE("xdgShellListener::onPing(self=", self, ", xdgShell=", xdgShell, ", serial=", serial, ").");

                if (*static_cast<XdgShellListener*>(self)->appCtx.xdgShell != xdgShell)
                {
                    MY_LOG_ERROR("xdgShellListener::onPing: appCtx.xdgShell != xdgShell");
                    return;
                }

                MY_LOG_WLCALL_VALUELESS(xdg_wm_base_pong(xdgShell, serial));
            }
        } xdgShellListener{ appCtx };
        if (const auto err = MY_LOG_WLCALL(xdg_wm_base_add_listener(*appCtx.xdgShell, &xdgShellListener.wlHandler, &xdgShellListener)); err != 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "Failed to set the XDG shell listener (xdg_wm_base_add_listener returned " + std::to_string(err) + ")"
            );

        // 5. wl_shm_pool doesn't have events yet

        // 6. wl_buffer only emits wl_buffer_release events currently, which aren't interesting at the moment

        // 7. wl_surface + xdg_surface + xdg_toplevel (of the main window)
        struct MainWindowSurfaceListener
        {
            WLAppCtx& appCtx;


            const wl_surface_listener wlHandlerSurface = {
                &onSurfaceEnterOutput,
                &onSurfaceLeaveOutput/*,
                    These are only supported since version 6 of the protocol
                &onSurfacePreferredBufferScale,
                &onSurfacePreferredBufferTransform*/
            };

            static void onSurfaceEnterOutput(void * const self, wl_surface * const surface, wl_output * const output)
            {
                MY_LOG_TRACE("mainWindowSurfaceListener::onSurfaceEnterOutput(self=", self, ", ",
                                                                             "surface=", surface, ", ",
                                                                             "output=", output,
                                                                             ").");
            }

            static void onSurfaceLeaveOutput(void * const self, wl_surface * const surface, wl_output * const output)
            {
                MY_LOG_TRACE("mainWindowSurfaceListener::onSurfaceLeaveOutput(self=", self, ", ",
                                                                             "surface=", surface, ", ",
                                                                             "output=", output,
                                                                             ").");
            }

            /* These are only supported since version 6 of the protocol
            static void onSurfacePreferredBufferScale(void * const self, wl_surface * const surface, const int factor);
            static void onSurfacePreferredBufferTransform(void * const self, wl_surface * const surface, const wl_output_transform transform);
            */


            const xdg_surface_listener wlHandlerXdgSurface = { &onXdgSurfaceConfigure };

            static void onXdgSurfaceConfigure(void * const self, xdg_surface * const xdgSurface, const uint32_t serial)
            {
                MY_LOG_TRACE("mainWindowSurfaceListener::onXdgSurfaceConfigure(self=", self, ", ",
                                                                              "xdgSurface=", xdgSurface, ", ",
                                                                              "serial=", serial,
                                                                              ").");

                // TODO: handle all the kinds of configure events and send 'ack_configure' requests back
            }


            const xdg_toplevel_listener wlHandlerXdgToplevel = {
                &onXdgTopLevelConfigure,
                &onXdgTopLevelClose,
                &onXdgTopLevelConfigureBounds
            };

            static void onXdgTopLevelConfigure(
                void * const self,
                xdg_toplevel * const xdgToplevel,
                const int32_t width,
                const int32_t height,
                wl_array * const states
            ) {
                MY_LOG_TRACE("mainWindowSurfaceListener::onXdgTopLevelConfigure(self=", self, ", ",
                                                                               "xdgToplevel=", xdgToplevel, ", ",
                                                                               "width=", width, ", ",
                                                                               "height=", height, ", ",
                                                                               "states=", states,
                                                                               ").");
            }

            static void onXdgTopLevelClose(void * const self, xdg_toplevel * const xdgToplevel)
            {
                MY_LOG_TRACE("mainWindowSurfaceListener::onXdgTopLevelClose(self=", self, ", ",
                                                                           "xdgToplevel=", xdgToplevel,
                                                                           ").");
                static_cast<MainWindowSurfaceListener*>(self)->appCtx.shouldExit = true;
            }

            static void onXdgTopLevelConfigureBounds(
                void * const self,
                xdg_toplevel * const xdgToplevel,
                const int32_t width,
                const int32_t height
            ) {
                MY_LOG_TRACE("mainWindowSurfaceListener::onXdgTopLevelConfigureBounds(self=", self, ", ",
                                                                                     "xdgToplevel=", xdgToplevel, ", ",
                                                                                     "width=", width, ", ",
                                                                                     "height=", height,
                                                                                     ").");
            }
        } mainWindowSurfaceListener{ appCtx };
        if (const auto err = MY_LOG_WLCALL(wl_surface_add_listener(*appCtx.mainWindow.surface, &mainWindowSurfaceListener.wlHandlerSurface, &mainWindowSurfaceListener)); err != 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "Failed to set the main window surface listener (wl_surface_add_listener returned " + std::to_string(err) + ")"
            );
        if (const auto err = MY_LOG_WLCALL(xdg_surface_add_listener(*appCtx.mainWindow.xdgSurface, &mainWindowSurfaceListener.wlHandlerXdgSurface, &mainWindowSurfaceListener)); err != 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "Failed to set the main window XDG surface listener (xdg_surface_add_listener returned " + std::to_string(err) + ")"
            );
        if (const auto err = MY_LOG_WLCALL(xdg_toplevel_add_listener(*appCtx.mainWindow.xdgToplevel, &mainWindowSurfaceListener.wlHandlerXdgToplevel, &mainWindowSurfaceListener)); err != 0)
            throw std::system_error(
                errno,
                std::system_category(),
                "Failed to set the main window XDG surface listener (xdg_toplevel_add_listener returned " + std::to_string(err) + ")"
            );

        // The listener of wl_surface::frame callback
        struct MainWindowRedrawHintListener
        {
            WLAppCtx& appCtx;
            const wl_callback_listener wlHandler = { &onSurfaceFrame };
            wl_callback* pendingCallback = nullptr;

            static void onSurfaceFrame(void * const selfP, wl_callback * const callback, uint32_t /*timestampMs*/)
            {
                auto& self = *static_cast<MainWindowRedrawHintListener*>(selfP);

                if (callback == self.pendingCallback)
                {
                    // callback will be destroyed by the compositor
                    self.appCtx.mainWindow.readyToBeRedrawn = true;
                    self.pendingCallback = nullptr;
                }
            }
        } mainWindowRedrawHintListener{ appCtx };

        // ============================================== END of Step N ===============================================

        // ========================================= Step N+1: the event loop =========================================
        appCtx.mainWindow.pendingBufferIdx = 0;

        ContentState lastRenderedState = contentState;

        while (!appCtx.shouldExit)
        {
            const bool contentHasChanged = (contentState != lastRenderedState);
            if ( (appCtx.mainWindow.mustBeRedrawn || contentHasChanged) && appCtx.mainWindow.readyToBeRedrawn)
            {
                MY_LOG_TRACE("Redrawing the main window (appCtx.mainWindow.mustBeRedrawn=", appCtx.mainWindow.mustBeRedrawn, ", contentHasChanged=", contentHasChanged, ")...");

                appCtx.mainWindow.mustBeRedrawn = false;

                // Installing a new wl_surface::frame listener
                appCtx.mainWindow.readyToBeRedrawn = false;
                mainWindowRedrawHintListener.pendingCallback = MY_LOG_WLCALL(wl_surface_frame(*appCtx.mainWindow.surface));
                if (mainWindowRedrawHintListener.pendingCallback == nullptr)
                    throw std::system_error{errno, std::system_category(), "Failed to obtain a wl_surface::frame callback for the main window"};
                if (const auto err = MY_LOG_WLCALL(wl_callback_add_listener(mainWindowRedrawHintListener.pendingCallback, &mainWindowRedrawHintListener.wlHandler, &mainWindowRedrawHintListener)); err != 0)
                    throw std::system_error(
                        errno,
                        std::system_category(),
                        "Failed to set the main window wl_surface::frame callback listener (wl_callback_add_listener returned " + std::to_string(err) + ")"
                    );

                // Rendering to the pending pixel buffer
                renderMainWindow(appCtx, contentState);
                // Attaching the pending pixel buffer to the surface
                MY_LOG_WLCALL_VALUELESS(wl_surface_attach(*appCtx.mainWindow.surface, appCtx.mainWindow.getPendingWLSideBuffer(), 0, 0));
                // Letting the server know that it should re-render the whole buffer
                MY_LOG_WLCALL_VALUELESS(wl_surface_damage_buffer(*appCtx.mainWindow.surface, 0, 0, appCtx.mainWindow.width, appCtx.mainWindow.height));
                // Commiting the current state of the main window (including the buffer content) so the server can now apply it
                MY_LOG_WLCALL_VALUELESS(wl_surface_commit(*appCtx.mainWindow.surface));

                appCtx.mainWindow.pendingBufferIdx = (appCtx.mainWindow.pendingBufferIdx + 1) % 2;

                lastRenderedState = contentState;
            }

            appCtx.shouldExit = appCtx.shouldExit ||
                (MY_LOG_WLCALL(wl_display_dispatch(*appCtx.connection)) == -1);
        }
        // ============================================= END of Step N+1 ==============================================
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


ContentState ContentState::movedFor(double offsetX, double offsetY) const
{
    return ContentState{
        viewportOffsetX + offsetX,
        viewportOffsetY + offsetY,
        viewportZoom,
        viewportZoomCenterLocalX,
        viewportZoomCenterLocalY
    };
}

bool operator==(const ContentState& lhs, const ContentState& rhs) noexcept
{
    return ( (lhs.viewportOffsetX == rhs.viewportOffsetX) &&
             (lhs.viewportOffsetY == rhs.viewportOffsetY) &&
             (lhs.viewportZoom == rhs.viewportZoom) &&
             (lhs.viewportZoomCenterLocalX == rhs.viewportZoomCenterLocalX) &&
             (lhs.viewportZoomCenterLocalY == rhs.viewportZoomCenterLocalY) );
}

bool operator!=(const ContentState& lhs, const ContentState& rhs) noexcept
{
    return !(lhs == rhs);
}


void renderMainWindow(WLAppCtx& appCtx, ContentState contentState)
{
    //if (contentState.contentZoom <= 0)
    //    throw std::range_error{ "contentState.contentZoom <= 0" };

    // Rendering the chess board pattern respecting the content's offsets and zoom

    constexpr auto cellSideBasicSize = 60 /*px*/;


    const int viewportOffsetXRound = static_cast<int>(std::round(contentState.viewportOffsetX));
    const double xOffsetDiff = viewportOffsetXRound - contentState.viewportOffsetX;

    const int viewportOffsetYRound = static_cast<int>(std::round(contentState.viewportOffsetY));
    const double yOffsetDiff = viewportOffsetYRound - contentState.viewportOffsetY;

    const auto sideZoom = std::sqrt(contentState.viewportZoom);

    // Adjusting the zoom center with respect to the viewport position change
    const unsigned zoomCenterLocalX = static_cast<unsigned>(
        std::clamp<double>(
            std::round(contentState.viewportZoomCenterLocalX + xOffsetDiff),
            0,
            appCtx.mainWindow.width - 1
        )
    );
    const unsigned zoomCenterLocalY = static_cast<unsigned>(
        std::clamp<double>(
            std::round(contentState.viewportZoomCenterLocalY + yOffsetDiff),
            0,
            appCtx.mainWindow.height - 1
        )
    );

    appCtx.mainWindow.drawVia([viewportOffsetXRound, viewportOffsetYRound, zoom = sideZoom, zoomCenterLocalX, zoomCenterLocalY](std::size_t x, std::size_t y, std::byte& b, std::byte& g, std::byte& r) {
        const auto xRelZoomCenter = (x > zoomCenterLocalX)
                                    ? static_cast<int>(x - zoomCenterLocalX)
                                    : -static_cast<int>(zoomCenterLocalX - x);
        const auto srcXRelZoomCenter = static_cast< std::remove_cv_t<decltype(xRelZoomCenter)> >(std::round(xRelZoomCenter / zoom));
        const auto srcXGlobal = static_cast<std::int64_t>(viewportOffsetXRound) +
                                static_cast<std::int64_t>(zoomCenterLocalX) +
                                static_cast<std::int64_t>(srcXRelZoomCenter);

        const auto yRelZoomCenter = (y > zoomCenterLocalY)
                                    ? static_cast<int>(y - zoomCenterLocalY)
                                    : -static_cast<int>(zoomCenterLocalY - y);
        const auto srcYRelZoomCenter = static_cast< std::remove_cv_t<decltype(yRelZoomCenter)> >(std::round(yRelZoomCenter / zoom));
        const auto srcYGlobal = static_cast<std::int64_t>(viewportOffsetYRound) +
                                static_cast<std::int64_t>(zoomCenterLocalY) +
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

