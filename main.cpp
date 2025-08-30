#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <vector>
#include <cstdint>
#include <string>

// Define proper types
typedef struct objc_object *id;
typedef struct objc_selector *SEL;
typedef struct objc_class *Class;
typedef bool BOOL;
typedef unsigned long NSUInteger;
typedef void (*IMP)(void);
typedef double CGFloat;

// Define NSRect structure and related constants
typedef struct _NSRect {
    double x;
    double y;
    double width;
    double height;
} NSRect;

typedef struct _NSPoint {
    double x;
    double y;
} NSPoint;

typedef struct _NSSize {
    double width;
    double height;
} NSSize;

// Window style mask constants
#define NSWindowStyleMaskTitled 1 << 0
#define NSWindowStyleMaskClosable 1 << 1
#define NSWindowStyleMaskMiniaturizable 1 << 2
#define NSWindowStyleMaskResizable 1 << 3

// Backing store constants
#define NSBackingStoreRetained 0
#define NSBackingStoreNonretained 1
#define NSBackingStoreBuffered 2

// Application activation policies
#define NSApplicationActivationPolicyRegular 0

// Autoresizing mask constants
#define NSViewWidthSizable 1 << 1
#define NSViewHeightSizable 1 << 4

// Helper functions
NSRect NSMakeRect(double x, double y, double width, double height) {
    NSRect rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

NSPoint NSMakePoint(double x, double y) {
    NSPoint point;
    point.x = x;
    point.y = y;
    return point;
}

NSSize NSMakeSize(double width, double height) {
    NSSize size;
    size.width = width;
    size.height = height;
    return size;
}

// Global image data
std::vector<std::uint32_t> imageData;
int imageWidth = 800;
int imageHeight = 600;

// Load your image data
void loadImageData() {
    // Example: create a simple gradient image
    imageData.resize(imageWidth * imageHeight);
    for (int y = 0; y < imageHeight; ++y) {
        for (int x = 0; x < imageWidth; ++x) {
            std::uint8_t r = static_cast<std::uint8_t>((float)x / imageWidth * 255);
            std::uint8_t g = static_cast<std::uint8_t>((float)y / imageHeight * 255);
            std::uint8_t b = static_cast<std::uint8_t>((float)(x + y) / (imageWidth + imageHeight) * 255);
            std::uint8_t a = 255;
            
            // ARGB format (macOS expects premultiplied alpha)
            imageData[y * imageWidth + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

// The windowShouldClose method implementation
BOOL windowShouldClose(id self, SEL _cmd, id sender) {
    id application = ((id(*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    ((void(*)(id, SEL, id))objc_msgSend)(application, sel_registerName("terminate:"), (id)0);
    return YES;
}

// Custom view drawRect method
void drawRect(id self, SEL _cmd, NSRect rect) {
    // Get view bounds
    SEL boundsSel = sel_registerName("bounds");
    typedef NSRect (*BoundsFunc)(id, SEL);
    BoundsFunc getBounds = (BoundsFunc)objc_msgSend;
    NSRect bounds = getBounds(self, boundsSel);
    
    if (imageData.empty())
        return;
    
    // Get graphics context
    id context = ((id(*)(Class, SEL))objc_msgSend)(objc_getClass("NSGraphicsContext"), sel_registerName("currentContext"));
    id cgContext = ((id(*)(id, SEL))objc_msgSend)(context, sel_registerName("CGContext"));
    
    // Cast to CGContextRef
    typedef struct CGContext *CGContextRef;
    CGContextRef contextRef = (CGContextRef)cgContext;
    
    // Draw the image using Core Graphics
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    
    CGContextSaveGState(contextRef);
    
    // Flip the coordinate system (macOS has origin at bottom-left)
    CGContextTranslateCTM(contextRef, 0, bounds.height);
    CGContextScaleCTM(contextRef, 1.0, -1.0);
    
    // Create CGImage from our raw data
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL, 
        imageData.data(), 
        imageWidth * imageHeight * 4, 
        NULL
    );
        
    CGBitmapInfo bitmapInfo = kCGImageAlphaFirst | kCGBitmapByteOrder32Big;
    
    CGImageRef imageRef = CGImageCreate(
        imageWidth, 
        imageHeight, 
        8, 
        32, 
        imageWidth * 4, 
        colorSpace, 
        bitmapInfo,
        provider, 
        NULL, 
        false, 
        kCGRenderingIntentDefault
    );
    
    if (imageRef) {
        // Draw the image scaled to fit the view bounds
        CGRect imageRect = CGRectMake(0, 0, bounds.width, bounds.height);
        CGContextDrawImage(contextRef, imageRect, imageRef);
        CGImageRelease(imageRef);
    }
    
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    
    CGContextRestoreGState(contextRef);
}

// Delegate class to handle window close events
Class createWindowDelegateClass() {
    Class delegateClass = objc_allocateClassPair((Class)objc_getClass("NSObject"), "WindowDelegate", 0);
    
    // Add windowShouldClose: method
    SEL windowShouldCloseSel = sel_registerName("windowShouldClose:");
    class_addMethod(delegateClass, windowShouldCloseSel, (IMP)windowShouldClose, "c@:@");
    
    objc_registerClassPair(delegateClass);
    return delegateClass;
}

int main() {
    // Load image data
    loadImageData();
    
    // Get classes
    Class NSApplicationClass = objc_getClass("NSApplication");
    Class NSWindowClass = objc_getClass("NSWindow");
    Class NSStringClass = objc_getClass("NSString");
    
    // Get selectors
    SEL sharedApplicationSel = sel_registerName("sharedApplication");
    SEL setActivationPolicySel = sel_registerName("setActivationPolicy:");
    SEL allocSel = sel_registerName("alloc");
    SEL initSel = sel_registerName("init");
    SEL initWithContentRectSel = sel_registerName("initWithContentRect:styleMask:backing:defer:");
    SEL setTitleSel = sel_registerName("setTitle:");
    SEL makeKeyAndOrderFrontSel = sel_registerName("makeKeyAndOrderFront:");
    SEL runSel = sel_registerName("run");
    SEL stringWithUTF8StringSel = sel_registerName("stringWithUTF8String:");
    SEL setDelegateSel = sel_registerName("setDelegate:");
    SEL setContentViewSel = sel_registerName("setContentView:");
    SEL setNeedsDisplaySel = sel_registerName("setNeedsDisplay:");
    SEL centerSel = sel_registerName("center");
    SEL frameSel = sel_registerName("frame");
    SEL boundsSel = sel_registerName("bounds");
    
    // Get shared application
    id application = ((id(*)(Class, SEL))objc_msgSend)(NSApplicationClass, sharedApplicationSel);
    
    // Set activation policy to regular application
    ((void(*)(id, SEL, long))objc_msgSend)(application, setActivationPolicySel, NSApplicationActivationPolicyRegular);
    
    // Create window with proper style mask
    NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | 
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    
    // Calculate window position (center of screen)
    Class NSScreenClass = objc_getClass("NSScreen");
    SEL mainScreenSel = sel_registerName("mainScreen");
    id mainScreen = ((id(*)(Class, SEL))objc_msgSend)(NSScreenClass, mainScreenSel);
    
    typedef NSRect (*FrameFunc)(id, SEL);
    FrameFunc getFrame = (FrameFunc)objc_msgSend;
    NSRect screenFrame = getFrame(mainScreen, frameSel);
    
    double x = (screenFrame.width - imageWidth) / 2;
    double y = (screenFrame.height - imageHeight) / 2;
    NSRect windowRect = NSMakeRect(x, y, imageWidth, imageHeight);
    
    // Allocate and initialize window
    id window = ((id(*)(Class, SEL))objc_msgSend)(NSWindowClass, allocSel);
    window = ((id(*)(id, SEL, NSRect, NSUInteger, NSUInteger, BOOL))objc_msgSend)(
        window, initWithContentRectSel, windowRect, styleMask, NSBackingStoreBuffered, NO);
    
    // Set title
    id titleString = ((id(*)(Class, SEL, const char*))objc_msgSend)(
        NSStringClass, stringWithUTF8StringSel, "C++ macOS Window with Image");
    ((void(*)(id, SEL, id))objc_msgSend)(window, setTitleSel, titleString);
    
    // Create and set window delegate to handle close events
    Class delegateClass = createWindowDelegateClass();
    id delegate = ((id(*)(Class, SEL))objc_msgSend)(delegateClass, allocSel);
    delegate = ((id(*)(id, SEL))objc_msgSend)(delegate, initSel);
    ((void(*)(id, SEL, id))objc_msgSend)(window, setDelegateSel, delegate);
    
    // Get the current content view bounds
    id contentView = ((id(*)(id, SEL))objc_msgSend)(window, sel_registerName("contentView"));
    NSRect contentBounds = getFrame(contentView, boundsSel);
    
    // Instead of creating a separate custom view, let's subclass the content view
    Class contentViewClass = objc_allocateClassPair((Class)objc_getClass("NSView"), "ContentView", 0);
    SEL drawRectSel = sel_registerName("drawRect:");
    class_addMethod(contentViewClass, drawRectSel, (IMP)drawRect, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
    objc_registerClassPair(contentViewClass);
    
    // Replace the content view
    id newContentView = ((id(*)(Class, SEL))objc_msgSend)(contentViewClass, allocSel);
    newContentView = ((id(*)(id, SEL, NSRect))objc_msgSend)(newContentView, initSel, contentBounds);
    ((void(*)(id, SEL, id))objc_msgSend)(window, setContentViewSel, newContentView);
    
    // Force the view to display immediately
    ((void(*)(id, SEL, BOOL))objc_msgSend)(newContentView, setNeedsDisplaySel, YES);
    
    // Show window
    ((void(*)(id, SEL, id))objc_msgSend)(window, makeKeyAndOrderFrontSel, (id)0);
    
    // Run application
    ((void(*)(id, SEL))objc_msgSend)(application, runSel);
    
    return 0;
}