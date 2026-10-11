#pragma once

#include <glm/vec4.hpp>
#include <map>
#include <string>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include "Output.h"
#include "WallpaperEngine/Render/Drivers/VideoDriver.h"

namespace WallpaperEngine::Render::Drivers::Output {
class X11Output final : public Output {
public:
    X11Output (ApplicationContext& context, VideoDriver& driver);
    ~X11Output () override;

    void reset () override;

    bool renderVFlip () const override;
    bool renderMultiple () const override;
    bool haveImageBuffer () const override;
    void* getImageBuffer () const override;
    uint32_t getImageBufferSize () const override;
    void updateRender () const override;

private:
    void loadScreenInfo ();
    void discoverOutputs (XRRScreenResources* screenResources);
    void validateOutputs () const;
    void initX11Background ();
    void free (bool restoreRootBackground);
    void captureRootBackground ();
    void releaseRootBackground (bool restoreRootBackground);
    void publishRootBackground ();

    Display* m_display = nullptr;
    Pixmap m_pixmap = None;
    Window m_root = None;
    GC m_gc = nullptr;
    char* m_imageData = nullptr;
    uint32_t m_imageSize = 0;
    XImage* m_image = nullptr;
    Atom m_rootPixmapAtom = None;
    Atom m_esetrootPixmapAtom = None;
    Pixmap m_previousRootPixmap = None;
    Pixmap m_previousEsetrootPixmap = None;
    bool m_rootBackgroundCaptured = false;
    bool m_rootBackgroundInstalled = false;
    std::vector<OutputViewport*> m_screens = {};
};
} // namespace WallpaperEngine::Render::Drivers::Output