
#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "win_local.h"
#include "rc/AFEditor_resource.h"
#include "rc/doom_resource.h"
#include "../../renderer/tr_local.h"


/*
====================
GLW_CreateWindowClasses
====================
*/
static void GLW_CreateWindowClasses(void) {
	WNDCLASS wc;

	//
	// register the window class if necessary
	//
	if (win32.windowClassRegistered) {
		return;
	}

	memset(&wc, 0, sizeof(wc));

	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = win32.hInstance;
	wc.hIcon = LoadIcon(win32.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (struct HBRUSH__*)COLOR_GRAYTEXT;
	wc.lpszMenuName = 0;
	wc.lpszClassName = WIN32_WINDOW_CLASS_NAME;

	if (!RegisterClass(&wc)) {
		common->FatalError("GLW_CreateWindow: could not register window class");
	}
	common->Printf("...registered window class\n");

	win32.windowClassRegistered = true;
}

/*
=======================
GLW_CreateWindow

Responsible for creating the Win32 window.
If cdsFullscreen is true, it won't have a border
=======================
*/
static bool GLW_CreateWindow(glimpParms_t parms) {
	int				stylebits;
	int				x, y, w, h;
	int				exstyle;

	//
	// compute width and height
	//
	if (parms.fullScreen) {
		exstyle = WS_EX_TOPMOST;
		stylebits = WS_POPUP | WS_VISIBLE | WS_SYSMENU;

		x = 0;
		y = 0;
		w = parms.width;
		h = parms.height;
	}
	else {
		RECT	r;

		// adjust width and height for window border
		r.bottom = parms.height;
		r.left = 0;
		r.top = 0;
		r.right = parms.width;

		exstyle = 0;
		stylebits = WINDOW_STYLE | WS_SYSMENU;
		AdjustWindowRect(&r, stylebits, FALSE);

		w = r.right - r.left;
		h = r.bottom - r.top;

		x = win32.win_xpos.GetInteger();
		y = win32.win_ypos.GetInteger();

		// adjust window coordinates if necessary 
		// so that the window is completely on screen
		if (x + w > win32.desktopWidth) {
			x = (win32.desktopWidth - w);
		}
		if (y + h > win32.desktopHeight) {
			y = (win32.desktopHeight - h);
		}
		if (x < 0) {
			x = 0;
		}
		if (y < 0) {
			y = 0;
		}
	}

	win32.hWnd = CreateWindowEx(
		exstyle,
		WIN32_WINDOW_CLASS_NAME,
		GAME_NAME,
		stylebits,
		x, y, w, h,
		NULL,
		NULL,
		win32.hInstance,
		NULL);

	if (!win32.hWnd) {
		common->Printf("^3GLW_CreateWindow() - Couldn't create window^0\n");
		return false;
	}

	::SetTimer(win32.hWnd, 0, 100, NULL);

	ShowWindow(win32.hWnd, SW_SHOW);
	UpdateWindow(win32.hWnd);
	common->Printf("...created window @ %d,%d (%dx%d)\n", x, y, w, h);

	SetForegroundWindow(win32.hWnd);
	SetFocus(win32.hWnd);

	glConfig.isFullscreen = parms.fullScreen;

	return true;
}

/*
===================
GLimp_Init

This is the platform specific OpenGL initialization function.  It
is responsible for loading OpenGL, initializing it,
creating a window of the appropriate size, doing
fullscreen manipulations, etc.  Its overall responsibility is
to make sure that a functional OpenGL subsystem is operating
when it returns to the ref.

If there is any failure, the renderer will revert back to safe
parameters and try again.
===================
*/
bool Fgl_Init(glimpParms_t parms) {
	const char* driverName;
	HDC		hDC;

	common->Printf("Initializing Fury subsystem\n");

	// check our desktop attributes
	hDC = GetDC(GetDesktopWindow());
	win32.desktopBitsPixel = GetDeviceCaps(hDC, BITSPIXEL);
	win32.desktopWidth = GetDeviceCaps(hDC, HORZRES);
	win32.desktopHeight = GetDeviceCaps(hDC, VERTRES);
	ReleaseDC(GetDesktopWindow(), hDC);

	// we can't run in a window unless it is 32 bpp
	if (win32.desktopBitsPixel < 32 && !parms.fullScreen) {
		common->Printf("^3Windowed mode requires 32 bit desktop depth^0\n");
		return false;
	}

	// create our window classes if we haven't already
	GLW_CreateWindowClasses();

	// try to change to fullscreen
	if (parms.fullScreen) {
		//if (!GLW_SetFullScreen(parms)) {
		//	GLimp_Shutdown();
		//	return false;
		//}
	}

	// try to create a window with the correct pixel format
	// and init the renderer context
	if (!GLW_CreateWindow(parms)) {
		Fgl_Shutdown();
		return false;
	}

	return true;
}


/*
===================
GLimp_SetScreenParms

Sets up the screen based on passed parms..
===================
*/
bool Fgl_SetScreenParms(glimpParms_t parms) {
	int exstyle;
	int stylebits;
	int x, y, w, h;
	DEVMODE dm;

	memset(&dm, 0, sizeof(dm));
	dm.dmSize = sizeof(dm);
	dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
	if (parms.displayHz != 0) {
		dm.dmDisplayFrequency = parms.displayHz;
		dm.dmFields |= DM_DISPLAYFREQUENCY;
	}

	win32.cdsFullscreen = parms.fullScreen;
	glConfig.isFullscreen = parms.fullScreen;

	if (parms.fullScreen) {
		exstyle = WS_EX_TOPMOST;
		stylebits = WS_POPUP | WS_VISIBLE | WS_SYSMENU;
		SetWindowLong(win32.hWnd, GWL_STYLE, stylebits);
		SetWindowLong(win32.hWnd, GWL_EXSTYLE, exstyle);
		dm.dmPelsWidth = parms.width;
		dm.dmPelsHeight = parms.height;
		dm.dmBitsPerPel = 32;
		x = y = w = h = 0;
	}
	else {
		RECT	r;

		// adjust width and height for window border
		r.bottom = parms.height;
		r.left = 0;
		r.top = 0;
		r.right = parms.width;

		w = r.right - r.left;
		h = r.bottom - r.top;

		x = win32.win_xpos.GetInteger();
		y = win32.win_ypos.GetInteger();

		// adjust window coordinates if necessary 
		// so that the window is completely on screen
		if (x + w > win32.desktopWidth) {
			x = (win32.desktopWidth - w);
		}
		if (y + h > win32.desktopHeight) {
			y = (win32.desktopHeight - h);
		}
		if (x < 0) {
			x = 0;
		}
		if (y < 0) {
			y = 0;
		}
		dm.dmPelsWidth = win32.desktopWidth;
		dm.dmPelsHeight = win32.desktopHeight;
		dm.dmBitsPerPel = win32.desktopBitsPixel;
		exstyle = 0;
		stylebits = WINDOW_STYLE | WS_SYSMENU;
		AdjustWindowRect(&r, stylebits, FALSE);
		SetWindowLong(win32.hWnd, GWL_STYLE, stylebits);
		SetWindowLong(win32.hWnd, GWL_EXSTYLE, exstyle);
		common->Printf("%i %i %i %i\n", x, y, w, h);
	}
	bool ret = (ChangeDisplaySettings(&dm, parms.fullScreen ? CDS_FULLSCREEN : 0) == DISP_CHANGE_SUCCESSFUL);
	SetWindowPos(win32.hWnd, parms.fullScreen ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, parms.fullScreen ? SWP_NOSIZE | SWP_NOMOVE : SWP_SHOWWINDOW);
	return ret;
}

/*
===================
GLimp_Shutdown

This routine does all OS specific shutdown procedures for the OpenGL
subsystem.
===================
*/
void Fgl_Shutdown(void) {
	const char* success[] = { "failed", "success" };
	int retVal;

	common->Printf("Shutting down OpenGL subsystem\n");

	// set current context to NULL
	if (qwglMakeCurrent) {
		retVal = qwglMakeCurrent(NULL, NULL) != 0;
		common->Printf("...wglMakeCurrent( NULL, NULL ): %s\n", success[retVal]);
	}

	// delete HGLRC
	if (win32.hGLRC) {
		retVal = qwglDeleteContext(win32.hGLRC) != 0;
		common->Printf("...deleting GL context: %s\n", success[retVal]);
		win32.hGLRC = NULL;
	}

	// release DC
	if (win32.hDC) {
		retVal = ReleaseDC(win32.hWnd, win32.hDC) != 0;
		common->Printf("...releasing DC: %s\n", success[retVal]);
		win32.hDC = NULL;
	}

	// destroy window
	if (win32.hWnd) {
		common->Printf("...destroying window\n");
		ShowWindow(win32.hWnd, SW_HIDE);
		DestroyWindow(win32.hWnd);
		win32.hWnd = NULL;
	}

	// reset display settings
	if (win32.cdsFullscreen) {
		common->Printf("...resetting display\n");
		ChangeDisplaySettings(0, 0);
		win32.cdsFullscreen = false;
	}

	// close the thread so the handle doesn't dangle
	if (win32.renderThreadHandle) {
		common->Printf("...closing smp thread\n");
		CloseHandle(win32.renderThreadHandle);
		win32.renderThreadHandle = NULL;
	}
}