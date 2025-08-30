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
typedef unsigned long NSUInteger;
typedef void (*IMP)(void);
typedef double CGFloat;
typedef struct CGContext *CGContextRef;

// Define NSRect structure and related constants
struct NSRect
{
    double x;
    double y;
    double width;
    double height;
};

struct NSPoint
{
    double x;
    double y;
};

struct NSSize
{
    double width;
    double height;
};

namespace WindowStyle
{
    constexpr NSUInteger Titled = 1 << 0;
    constexpr NSUInteger Closable = 1 << 1;
    constexpr NSUInteger Miniaturizable = 1 << 2;
    constexpr NSUInteger Resizable = 1 << 3;
}

namespace BackingStore
{
    constexpr NSUInteger Retained = 0;
    constexpr NSUInteger Nonretained = 1;
    constexpr NSUInteger Buffered = 2;
}

namespace AppActivation
{
    constexpr NSUInteger Regular = 0;
}

// Helper functions
NSRect NSMakeRect(double x, double y, double width, double height)
{
    return { x, y, width, height };
}

NSPoint NSMakePoint(double x, double y)
{
    return { x, y };
}

NSSize NSMakeSize(double width, double height)
{
    return { width, height };
}

// Safe Objective-C message sending wrapper
template<typename ReturnType, typename... Args>
ReturnType sendMessage(id receiver, const char* selectorName, Args... args)
{
    SEL selector = sel_registerName(selectorName);
    return ((ReturnType(*)(id, SEL, Args...))objc_msgSend)(receiver, selector, args...);
}

template<typename ReturnType, typename... Args>
ReturnType sendClassMessage(Class cls, const char* selectorName, Args... args)
{
    SEL selector = sel_registerName(selectorName);
    return ((ReturnType(*)(Class, SEL, Args...))objc_msgSend)(cls, selector, args...);
}

// Convenience function to get classes
Class getClass(const char* className)
{
    return objc_getClass(className);
}

// Global image data
std::vector<std::uint32_t> gImageData;
constexpr int gImageWidth = 800;
constexpr int gImageHeight = 600;

// Load your image data
void loadImageData()
{
    gImageData.resize(gImageWidth * gImageHeight);
    for (int y = 0; y < gImageHeight; ++y) {
        for (int x = 0; x < gImageWidth; ++x) {
            std::uint8_t r = static_cast<std::uint8_t>((float)x / gImageWidth * 255);
            std::uint8_t g = static_cast<std::uint8_t>((float)y / gImageHeight * 255);
            std::uint8_t b = static_cast<std::uint8_t>((float)(x + y) / (gImageWidth + gImageHeight) * 255);
            std::uint8_t a = 255;
            
            // ARGB format (macOS expects premultiplied alpha)
            gImageData[y * gImageWidth + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

// The windowShouldClose method implementation
BOOL windowShouldClose(id self, SEL _cmd, id sender)
{
    id application = sendClassMessage<id>(getClass("NSApplication"), "sharedApplication");
    sendMessage<void>(application, "terminate:", nullptr);
    return YES;
}

// Custom view drawRect method
void drawRect(id self, SEL _cmd, NSRect rect)
{
    if (gImageData.empty())
        return;

    // Get view bounds
    NSRect bounds = sendMessage<NSRect>(self, "bounds");
    
    // Get graphics context
    id context = sendClassMessage<id>(getClass("NSGraphicsContext"), "currentContext");
    id cgContext = sendMessage<id>(context, "CGContext");
    
    // Cast to CGContextRef
    CGContextRef contextRef = (CGContextRef)cgContext;
    
    // Draw the image using Core Graphics
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    
    CGContextSaveGState(contextRef);
    
    // Flip the coordinate system (macOS has origin at bottom-left)
    CGContextTranslateCTM(contextRef, 0, bounds.height);
    CGContextScaleCTM(contextRef, 1.0, -1.0);
    
    // Create CGImage from our raw data
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, 
        gImageData.data(), 
        gImageWidth * gImageHeight * 4, 
        nullptr
    );
        
    CGBitmapInfo bitmapInfo = kCGImageAlphaFirst | kCGBitmapByteOrder32Big;
    
    CGImageRef imageRef = CGImageCreate(
        gImageWidth, 
        gImageHeight, 
        8, 
        32, 
        gImageWidth * 4, 
        colorSpace, 
        bitmapInfo,
        provider, 
        nullptr, 
        NO, 
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
Class createWindowDelegateClass()
{
    Class delegateClass = objc_allocateClassPair(getClass("NSObject"), "WindowDelegate", 0);
    
    // Add windowShouldClose: method
    SEL windowShouldCloseSel = sel_registerName("windowShouldClose:");
    class_addMethod(delegateClass, windowShouldCloseSel, (IMP)windowShouldClose, "c@:@");
    objc_registerClassPair(delegateClass);
    return delegateClass;
}

// Create content view class
Class createContentViewClass()
{
    Class contentViewClass = objc_allocateClassPair(getClass("NSView"), "ContentView", 0);
    SEL drawRectSel = sel_registerName("drawRect:");
    class_addMethod(contentViewClass, drawRectSel, (IMP)drawRect, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
    objc_registerClassPair(contentViewClass);
    return contentViewClass;
}

int main()
{
    // Load image data
    loadImageData();
    
    // Get shared application
    id application = sendClassMessage<id>(getClass("NSApplication"), "sharedApplication");
    sendMessage<void>(application, "setActivationPolicy:", AppActivation::Regular);

    // Calculate window position (center of screen)
    id mainScreen = sendClassMessage<id>(getClass("NSScreen"), "mainScreen");
    NSRect screenFrame = sendMessage<NSRect>(mainScreen, "frame");
    
    NSRect windowRect = NSMakeRect(
        (screenFrame.width - gImageWidth) / 2, 
        (screenFrame.height - gImageHeight) / 2, 
        gImageWidth, 
        gImageHeight
    );
    
    NSUInteger styleMask = WindowStyle::Titled 
        | WindowStyle::Closable 
        | WindowStyle::Miniaturizable 
        | WindowStyle::Resizable;

    // Allocate and initialize window
    id window = sendClassMessage<id>(getClass("NSWindow"), "alloc");
    window = sendMessage<id>(
        window,
        "initWithContentRect:styleMask:backing:defer:",
        windowRect,
        styleMask,
        BackingStore::Buffered,
        NO
    );
    
    // Set title
    id titleString = sendClassMessage<id>(
        getClass("NSString"),
        "stringWithUTF8String:",
        "C++ macOS Window with Image"
    );
    sendMessage<void>(window, "setTitle:", titleString);

    // Create and set window delegate to handle close events
    Class delegateClass = createWindowDelegateClass();
    id delegate = sendClassMessage<id>(delegateClass, "alloc");
    delegate = sendMessage<id>(delegate, "init");
    sendMessage<void>(window, "setDelegate:", delegate);
    
    // Get the current content view bounds
    id contentView = sendMessage<id>(window, "contentView");
    NSRect contentBounds = sendMessage<NSRect>(contentView, "bounds");
    
    // Instead of creating a separate custom view, let's subclass the content view
    Class contentViewClass = createContentViewClass();
    
    // Replace the content view
    id newContentView = sendClassMessage<id>(contentViewClass, "alloc");
    newContentView = sendMessage<id>(newContentView, "initWithFrame:", contentBounds);
    sendMessage<void>(window, "setContentView:", newContentView);
    sendMessage<void>(newContentView, "setNeedsDisplay:", YES);
    
    // Show window and run application
    sendMessage<void>(window, "makeKeyAndOrderFront:", nullptr);
    sendMessage<void>(application, "run");
    
    return 0;
}
