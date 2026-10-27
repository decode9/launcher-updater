/*
 * gdiplus.h - The GDI+ flat API subset the window draws with, declared by hand (no SDK).
 * Every function must also be listed in src/platform/win32/gdiplus.def. GDI+ takes float
 * coordinates; the build targets SSE2, so float <-> int conversions are inline instructions
 * (no __ftol2), and rt.c defines _fltused.
 */
#ifndef LAUNCHER_WIN32_UI_GDIPLUS_H
#define LAUNCHER_WIN32_UI_GDIPLUS_H

#include "winui.h"

typedef int GpStatus; /* 0 = Ok */
typedef uint32_t ARGB;
typedef struct GpGraphics GpGraphics;
typedef struct GpBrush GpBrush;
typedef struct GpPen GpPen;
typedef struct GpPath GpPath;
typedef struct GpImage GpImage;
typedef struct GpFontCollection GpFontCollection;
typedef struct GpFontFamily GpFontFamily;
typedef struct GpFont GpFont;
typedef struct GpStringFormat GpStringFormat;

typedef struct {
    uint32_t GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct {
    float X, Y, Width, Height;
} GpRectF;

#define GP_SMOOTHING_ANTIALIAS 4
#define GP_TEXT_ANTIALIAS_GRIDFIT 3
#define GP_PIXEL_OFFSET_HALF 4
#define GP_INTERPOLATION_HQ_BICUBIC 7
#define GP_FILL_ALTERNATE 0
#define GP_UNIT_PIXEL 2
#define GP_FONT_REGULAR 0
#define GP_FONT_BOLD 1
#define GP_ALIGN_NEAR 0
#define GP_ALIGN_CENTER 1
#define GP_ALIGN_FAR 2
#define GP_FORMAT_NOWRAP 0x00001000
#define GP_TRIM_ELLIPSIS_CHAR 3
#define GP_COMBINE_REPLACE 0
#define GP_COMBINE_INTERSECT 1
#define GP_PIXFMT_32BPP_PARGB 0x000E200B

IMPORT GpStatus WINAPI GdiplusStartup(uintptr_t *, const GdiplusStartupInput *, void *);
IMPORT void WINAPI GdiplusShutdown(uintptr_t);
IMPORT GpStatus WINAPI GdipCreateFromHDC(HDC, GpGraphics **);
IMPORT GpStatus WINAPI GdipDeleteGraphics(GpGraphics *);
IMPORT GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *, int);
IMPORT GpStatus WINAPI GdipSetTextRenderingHint(GpGraphics *, int);
IMPORT GpStatus WINAPI GdipSetPixelOffsetMode(GpGraphics *, int);
IMPORT GpStatus WINAPI GdipSetInterpolationMode(GpGraphics *, int);
IMPORT GpStatus WINAPI GdipGraphicsClear(GpGraphics *, ARGB);
IMPORT GpStatus WINAPI GdipCreateSolidFill(ARGB, GpBrush **);
IMPORT GpStatus WINAPI GdipDeleteBrush(GpBrush *);
IMPORT GpStatus WINAPI GdipCreatePen1(ARGB, float, int, GpPen **);
IMPORT GpStatus WINAPI GdipDeletePen(GpPen *);
IMPORT GpStatus WINAPI GdipCreatePath(int, GpPath **);
IMPORT GpStatus WINAPI GdipDeletePath(GpPath *);
IMPORT GpStatus WINAPI GdipAddPathArc(GpPath *, float, float, float, float, float, float);
IMPORT GpStatus WINAPI GdipAddPathRectangle(GpPath *, float, float, float, float);
IMPORT GpStatus WINAPI GdipClosePathFigure(GpPath *);
IMPORT GpStatus WINAPI GdipFillPath(GpGraphics *, GpBrush *, GpPath *);
IMPORT GpStatus WINAPI GdipDrawPath(GpGraphics *, GpPen *, GpPath *);
IMPORT GpStatus WINAPI GdipSetClipPath(GpGraphics *, GpPath *, int);
IMPORT GpStatus WINAPI GdipSetClipRectI(GpGraphics *, int, int, int, int, int);
IMPORT GpStatus WINAPI GdipResetClip(GpGraphics *);
IMPORT GpStatus WINAPI GdipDrawImageRectI(GpGraphics *, GpImage *, int, int, int, int);
IMPORT GpStatus WINAPI GdipCreateBitmapFromStream(IStream *, GpImage **);
IMPORT GpStatus WINAPI GdipCreateBitmapFromScan0(int, int, int, int, uint8_t *, GpImage **);
IMPORT GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *, GpGraphics **);
IMPORT GpStatus WINAPI GdipGetImageWidth(GpImage *, UINT *);
IMPORT GpStatus WINAPI GdipGetImageHeight(GpImage *, UINT *);
IMPORT GpStatus WINAPI GdipDisposeImage(GpImage *);
IMPORT GpStatus WINAPI GdipNewPrivateFontCollection(GpFontCollection **);
IMPORT GpStatus WINAPI GdipDeletePrivateFontCollection(GpFontCollection **);
IMPORT GpStatus WINAPI GdipPrivateAddMemoryFont(GpFontCollection *, const void *, int);
IMPORT GpStatus WINAPI GdipCreateFontFamilyFromName(const wchar_t *, GpFontCollection *, GpFontFamily **);
IMPORT GpStatus WINAPI GdipGetGenericFontFamilySansSerif(GpFontFamily **);
IMPORT GpStatus WINAPI GdipDeleteFontFamily(GpFontFamily *);
IMPORT GpStatus WINAPI GdipCreateFont(const GpFontFamily *, float, int, int, GpFont **);
IMPORT GpStatus WINAPI GdipDeleteFont(GpFont *);
IMPORT GpStatus WINAPI GdipCreateStringFormat(int, unsigned short, GpStringFormat **);
IMPORT GpStatus WINAPI GdipSetStringFormatAlign(GpStringFormat *, int);
IMPORT GpStatus WINAPI GdipSetStringFormatLineAlign(GpStringFormat *, int);
IMPORT GpStatus WINAPI GdipSetStringFormatTrimming(GpStringFormat *, int);
IMPORT GpStatus WINAPI GdipDeleteStringFormat(GpStringFormat *);
IMPORT GpStatus WINAPI GdipDrawString(GpGraphics *, const wchar_t *, int, const GpFont *, const GpRectF *,
                                      const GpStringFormat *, const GpBrush *);

#endif
