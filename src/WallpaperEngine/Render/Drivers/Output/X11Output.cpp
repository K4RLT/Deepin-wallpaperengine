#include "X11Output.h"
#include "GLFWOutputViewport.h"
#include "WallpaperEngine/Logging/Log.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cstdlib>
#include <limits>

using namespace WallpaperEngine::Render::Drivers::Output;

static Pixmap getPixmapProperty (Display* display, Window window, Atom property) {
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;

    if (XGetWindowProperty (
	    display, window, property, 0, 1, False, XA_PIXMAP, &actualType, &actualFormat, &itemCount, &bytesAfter,
	    &data
	)
	!= Success) {
	return None;
    }

    Pixmap pixmap = None;
    if (actualType == XA_PIXMAP && actualFormat == 32 && itemCount == 1 && data != nullptr) {
	pixmap = *reinterpret_cast<Pixmap*> (data);
    }

    if (data != nullptr) {
	XFree (data);
    }

    return pixmap;
}

void CustomXIOErrorExitHandler (Display* dsp, void* userdata) {
    const auto context = static_cast<X11Output*> (userdata);

    sLog.debugerror ("Critical XServer error detected. Attempting to recover...");

    // refetch all the resources
    context->reset ();
}

int CustomXErrorHandler (Display* dpy, XErrorEvent* event) {
    sLog.debugerror ("Detected X error");

    return 0;
}

int CustomXIOErrorHandler (Display* dsp) {
    sLog.debugerror ("Detected X error");

    return 0;
}

X11Output::X11Output (ApplicationContext& context, VideoDriver& driver) :
    Output (context, driver), m_display (nullptr), m_pixmap (None), m_root (None), m_gc (None), m_imageData (nullptr),
    m_imageSize (0), m_image (nullptr) {
    // do not use previous handler, it might stop the app under weird circumstances
    XSetErrorHandler (CustomXErrorHandler);
    XSetIOErrorHandler (CustomXIOErrorHandler);

    this->loadScreenInfo ();
}

X11Output::~X11Output () { this->free (true); }

void X11Output::reset () {
    // first free whatever we have right now
    this->free (false);
    // re-load screen info
    this->loadScreenInfo ();
    // do the same for the detector
    // TODO: BRING BACK THIS FUNCTIONALITY
    // this->m_driver.getFullscreenDetector ().reset ();
}

void X11Output::free (bool restoreRootBackground) {
    // delete owned viewport objects (m_viewports holds non-owning aliases)
    for (const auto& screen : this->m_screens) {
	delete screen;
    }

    this->m_screens.clear ();
    this->m_viewports.clear ();

    if (this->m_display != nullptr) {
	this->releaseRootBackground (restoreRootBackground);

	if (this->m_image != nullptr) {
	    XDestroyImage (this->m_image);
	    this->m_image = nullptr;
	    this->m_imageData = nullptr;
	}
	if (this->m_gc != nullptr) {
	    XFreeGC (this->m_display, this->m_gc);
	    this->m_gc = nullptr;
	}
	if (this->m_pixmap != None) {
	    XFreePixmap (this->m_display, this->m_pixmap);
	    this->m_pixmap = None;
	}

	XCloseDisplay (this->m_display);
	this->m_display = nullptr;
    }

    this->m_imageSize = 0;
    this->m_root = None;
}

void* X11Output::getImageBuffer () const { return this->m_imageData; }

bool X11Output::renderVFlip () const { return false; }

bool X11Output::renderMultiple () const { return this->m_viewports.size () > 1; }

bool X11Output::haveImageBuffer () const { return true; }

uint32_t X11Output::getImageBufferSize () const { return this->m_imageSize; }

void X11Output::loadScreenInfo () {
    this->m_display = XOpenDisplay (nullptr);
    if (this->m_display == nullptr) {
	sLog.exception ("Cannot connect to the X11 display");
    }

    this->captureRootBackground ();

    // set the error handling to try and recover from X disconnections
#ifdef HAVE_XSETIOERROREXITHANDLER
    XSetIOErrorExitHandler (this->m_display, CustomXIOErrorExitHandler, this);
#endif /* HAVE_XSETIOERROREXITHANDLER */

    int xrandr_result, xrandr_error;

    if (!XRRQueryExtension (this->m_display, &xrandr_result, &xrandr_error)) {
	sLog.error ("XRandr is not present, cannot detect specified screens, running in window mode");
	return;
    }

    this->m_root = DefaultRootWindow (this->m_display);
    this->m_fullWidth = DisplayWidth (this->m_display, DefaultScreen (this->m_display));
    this->m_fullHeight = DisplayHeight (this->m_display, DefaultScreen (this->m_display));
    XRRScreenResources* screenResources = XRRGetScreenResources (this->m_display, DefaultRootWindow (this->m_display));

    if (screenResources == nullptr) {
	sLog.error ("Cannot detect screen sizes using xrandr, running in window mode");
	return;
    }

    discoverOutputs (screenResources);
    XRRFreeScreenResources (screenResources);
    validateOutputs ();
    initX11Background ();
}

void X11Output::discoverOutputs (XRRScreenResources* screenResources) {
    for (int i = 0; i < screenResources->noutput; i++) {
	XRROutputInfo* info = XRRGetOutputInfo (this->m_display, screenResources, screenResources->outputs[i]);

	// screen not in use, ignore it
	if (info == nullptr || info->connection != RR_Connected) {
	    if (info != nullptr) {
		XRRFreeOutputInfo (info);
	    }
	    continue;
	}

	XRRCrtcInfo* crtc = info->crtc != None ? XRRGetCrtcInfo (this->m_display, screenResources, info->crtc) : nullptr;

	// screen not active, ignore it
	if (crtc == nullptr) {
	    XRRFreeOutputInfo (info);
	    continue;
	}

	// check if this screen is part of a span group
	bool inSpanGroup = false;
	for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	    for (const auto& screen : spanGroup.screens) {
		if (screen == info->name) {
		    inSpanGroup = true;
		    break;
		}
	    }
	    if (inSpanGroup) {
		break;
	    }
	}

	// only keep info of registered screens
	if (inSpanGroup
	    || this->m_context.settings.general.screenBackgrounds.find (info->name)
		!= this->m_context.settings.general.screenBackgrounds.end ()) {
	    sLog.out (
		"Found requested screen: ", info->name, " -> ", crtc->x, "x", crtc->y, ":", crtc->width, "x",
		crtc->height
	    );

	    auto* vp = new GLFWOutputViewport { { crtc->x, crtc->y, crtc->width, crtc->height }, info->name };
	    vp->globalPosition = { crtc->x, crtc->y };
	    vp->logicalSize = { crtc->width, crtc->height };
	    this->m_screens.push_back (vp);
	    this->m_viewports[info->name] = vp;
	}

	XRRFreeCrtcInfo (crtc);
	XRRFreeOutputInfo (info);
    }
}

void X11Output::validateOutputs () const {
    bool any = false;

    for (const auto& o : this->m_screens) {
	const auto cur = this->m_context.settings.general.screenBackgrounds.find (o->name);

	if (cur != this->m_context.settings.general.screenBackgrounds.end ()) {
	    any = true;
	    break;
	}

	// also check span groups
	for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	    for (const auto& screen : spanGroup.screens) {
		if (screen == o->name) {
		    any = true;
		    break;
		}
	    }
	    if (any) {
		break;
	    }
	}
	if (any) {
	    break;
	}
    }

    if (!any) {
	sLog.error ("No outputs could be initialized, please check the parameters and try again");
	sLog.error ("Detected outputs:");

	for (const auto& o : this->m_screens) {
	    sLog.error ("  ", o->name);
	}

	sLog.error ("Requested: ");

	for (const auto& o : this->m_context.settings.general.screenBackgrounds | std::views::keys) {
	    sLog.error ("  ", o);
	}

	sLog.exception ("Cannot continue...");
    }
}

void X11Output::initX11Background () {
    const int screen = DefaultScreen (this->m_display);
    const int depth = DefaultDepth (this->m_display, screen);

    // create pixmap so we can draw things in there
    this->m_pixmap = XCreatePixmap (this->m_display, this->m_root, this->m_fullWidth, this->m_fullHeight, depth);
    if (this->m_pixmap == None) {
	sLog.exception ("Cannot create the X11 root background pixmap");
    }

    this->m_gc = XCreateGC (this->m_display, this->m_pixmap, 0, nullptr);
    if (this->m_gc == nullptr) {
	XFreePixmap (this->m_display, this->m_pixmap);
	this->m_pixmap = None;
	sLog.exception ("Cannot create the X11 root background graphics context");
    }

    // pre-fill it with black
    XFillRectangle (this->m_display, this->m_pixmap, this->m_gc, 0, 0, this->m_fullWidth, this->m_fullHeight);

    // create an image so we can copy it over
    this->m_image = XCreateImage (
	this->m_display, DefaultVisual (this->m_display, screen), depth, ZPixmap, 0, nullptr, this->m_fullWidth,
	this->m_fullHeight, BitmapPad (this->m_display), 0
    );
    if (this->m_image == nullptr) {
	sLog.exception ("Cannot create the X11 root background image");
    }

    const size_t imageSize = static_cast<size_t> (this->m_image->bytes_per_line) * this->m_fullHeight;
    if (imageSize > std::numeric_limits<uint32_t>::max ()) {
	XDestroyImage (this->m_image);
	this->m_image = nullptr;
	sLog.exception ("X11 root background image exceeds the supported buffer size");
    }

    this->m_imageData = static_cast<char*> (std::calloc (imageSize, 1));
    if (this->m_imageData == nullptr) {
	XDestroyImage (this->m_image);
	this->m_image = nullptr;
	sLog.exception ("Cannot allocate the X11 root background image buffer");
    }
    this->m_image->data = this->m_imageData;
    this->m_imageSize = static_cast<uint32_t> (imageSize);

    // Publish ownership before output reset or shutdown can release this X resource.
    XSetWindowBackgroundPixmap (this->m_display, this->m_root, this->m_pixmap);
    this->publishRootBackground ();

    // setup driver's render changing the window's size
    this->m_driver.resizeWindow ({ this->m_fullWidth, this->m_fullHeight });
}

void X11Output::updateRender () const {
    // put the image back into the screen
    XPutImage (
	this->m_display, this->m_pixmap, this->m_gc, this->m_image, 0, 0, 0, 0, this->m_fullWidth, this->m_fullHeight
    );

    XClearWindow (this->m_display, this->m_root);
    XFlush (this->m_display);
}

void X11Output::captureRootBackground () {
    if (this->m_rootBackgroundCaptured) {
	return;
    }

    this->m_root = DefaultRootWindow (this->m_display);
    this->m_rootPixmapAtom = XInternAtom (this->m_display, "_XROOTPMAP_ID", False);
    this->m_esetrootPixmapAtom = XInternAtom (this->m_display, "ESETROOT_PMAP_ID", False);
    this->m_previousRootPixmap = getPixmapProperty (this->m_display, this->m_root, this->m_rootPixmapAtom);
    this->m_previousEsetrootPixmap = getPixmapProperty (this->m_display, this->m_root, this->m_esetrootPixmapAtom);
    this->m_rootBackgroundCaptured = true;
}

void X11Output::releaseRootBackground (bool restoreRootBackground) {
    if (!this->m_rootBackgroundInstalled || this->m_pixmap == None) {
	return;
    }

    const bool ownsRootPixmap = getPixmapProperty (this->m_display, this->m_root, this->m_rootPixmapAtom) == this->m_pixmap;
    const bool ownsEsetrootPixmap
	= getPixmapProperty (this->m_display, this->m_root, this->m_esetrootPixmapAtom) == this->m_pixmap;

    if (ownsRootPixmap || ownsEsetrootPixmap) {
	if (restoreRootBackground) {
	    const Pixmap previousPixmap
		= this->m_previousRootPixmap != None ? this->m_previousRootPixmap : this->m_previousEsetrootPixmap;
	    XSetWindowBackgroundPixmap (this->m_display, this->m_root, previousPixmap);
	} else {
	    XSetWindowBackgroundPixmap (this->m_display, this->m_root, None);
	}

	if (ownsRootPixmap) {
	    if (restoreRootBackground && this->m_previousRootPixmap != None) {
		XChangeProperty (
		    this->m_display, this->m_root, this->m_rootPixmapAtom, XA_PIXMAP, 32, PropModeReplace,
		    reinterpret_cast<unsigned char*> (&this->m_previousRootPixmap), 1
		);
	    } else {
		XDeleteProperty (this->m_display, this->m_root, this->m_rootPixmapAtom);
	    }
	}

	if (ownsEsetrootPixmap) {
	    if (restoreRootBackground && this->m_previousEsetrootPixmap != None) {
		XChangeProperty (
		    this->m_display, this->m_root, this->m_esetrootPixmapAtom, XA_PIXMAP, 32, PropModeReplace,
		    reinterpret_cast<unsigned char*> (&this->m_previousEsetrootPixmap), 1
		);
	    } else {
		XDeleteProperty (this->m_display, this->m_root, this->m_esetrootPixmapAtom);
	    }
	}

	XClearWindow (this->m_display, this->m_root);
	XFlush (this->m_display);
    }

    this->m_rootBackgroundInstalled = false;
}

void X11Output::publishRootBackground () {
    XChangeProperty (
	this->m_display, this->m_root, this->m_rootPixmapAtom, XA_PIXMAP, 32, PropModeReplace,
	reinterpret_cast<unsigned char*> (&this->m_pixmap), 1
    );
    XChangeProperty (
	this->m_display, this->m_root, this->m_esetrootPixmapAtom, XA_PIXMAP, 32, PropModeReplace,
	reinterpret_cast<unsigned char*> (&this->m_pixmap), 1
    );
    this->m_rootBackgroundInstalled = true;
}
