#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <vector>
#include <cstdint>
#include <string>

// Define proper types
using ObjcObject = objc_object*;
using ObjcSelector = objc_selector*;
using ObjcClass = objc_class*;
using ObjcMethodImplementation = void (*)(void);
using NSUInteger = unsigned long;
using CGFloat = double;
using CGContextRef = CGContext*;

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
ReturnType sendMessage(ObjcObject receiver, const char* selectorName, Args... args)
{
    ObjcSelector selector = sel_registerName(selectorName);
    return reinterpret_cast<ReturnType(*)(ObjcObject, ObjcSelector, Args...)>(objc_msgSend)(receiver, selector, args...);
}

template<typename ReturnType, typename... Args>
ReturnType sendClassMessage(ObjcClass cls, const char* selectorName, Args... args)
{
    ObjcSelector selector = sel_registerName(selectorName);
    return reinterpret_cast<ReturnType(*)(ObjcClass, ObjcSelector, Args...)>(objc_msgSend)(cls, selector, args...);
}

// Convenience function to get classes
ObjcClass getClass(const char* className)
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
BOOL windowShouldClose(ObjcObject self, ObjcSelector _cmd, ObjcObject sender)
{
    ObjcObject application = sendClassMessage<ObjcObject>(getClass("NSApplication"), "sharedApplication");
    sendMessage<void>(application, "terminate:", nullptr);
    return YES;
}

// Custom view drawRect method
void drawRect(ObjcObject self, ObjcSelector _cmd, NSRect rect)
{
    if (gImageData.empty())
        return;

    // Get view bounds
    NSRect bounds = sendMessage<NSRect>(self, "bounds");
    
    // Get graphics context
    ObjcObject context = sendClassMessage<ObjcObject>(getClass("NSGraphicsContext"), "currentContext");
    ObjcObject cgContext = sendMessage<ObjcObject>(context, "CGContext");
    
    // Cast to CGContextRef
    CGContextRef contextRef = reinterpret_cast<CGContextRef>(cgContext);
    
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
        
    CGImageRef imageRef = CGImageCreate(
        gImageWidth, 
        gImageHeight, 
        8, 
        32, 
        gImageWidth * 4, 
        colorSpace, 
        kCGImageAlphaFirst | kCGBitmapByteOrder32Big,
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
ObjcClass createWindowDelegateClass()
{
    ObjcClass delegateClass = objc_allocateClassPair(getClass("NSObject"), "WindowDelegate", 0);
    
    // Add windowShouldClose: method
    ObjcSelector windowShouldCloseSel = sel_registerName("windowShouldClose:");
    class_addMethod(
        delegateClass, 
        windowShouldCloseSel, 
        reinterpret_cast<ObjcMethodImplementation>(windowShouldClose), 
        "c@:@"
    );
    objc_registerClassPair(delegateClass);
    return delegateClass;
}

// Create content view class
ObjcClass createContentViewClass()
{
    ObjcClass contentViewClass = objc_allocateClassPair(getClass("NSView"), "ContentView", 0);
    ObjcSelector drawRectSel = sel_registerName("drawRect:");
    class_addMethod(
        contentViewClass, 
        drawRectSel, 
        reinterpret_cast<ObjcMethodImplementation>(drawRect), 
        "v@:{CGRect={CGPoint=dd}{CGSize=dd}}"
    );
    objc_registerClassPair(contentViewClass);
    return contentViewClass;
}

int main()
{
    // Load image data
    loadImageData();
    
    // Get shared application
    ObjcObject application = sendClassMessage<ObjcObject>(getClass("NSApplication"), "sharedApplication");
    sendMessage<void>(application, "setActivationPolicy:", AppActivation::Regular);

    // Calculate window position (center of screen)
    ObjcObject mainScreen = sendClassMessage<ObjcObject>(getClass("NSScreen"), "mainScreen");
    NSRect screenFrame = sendMessage<NSRect>(mainScreen, "frame");
    
    NSRect windowRect = NSMakeRect(
        (screenFrame.width - gImageWidth) / 2, 
        (screenFrame.height - gImageHeight) / 2, 
        gImageWidth, 
        gImageHeight
    );
    
    // Allocate and initialize window
    ObjcObject window = sendClassMessage<ObjcObject>(getClass("NSWindow"), "alloc");
    window = sendMessage<ObjcObject>(
        window,
        "initWithContentRect:styleMask:backing:defer:",
        windowRect,
        WindowStyle::Titled | WindowStyle::Closable | WindowStyle::Miniaturizable | WindowStyle::Resizable,
        BackingStore::Buffered,
        NO
    );
    
    // Set title
    ObjcObject titleString = sendClassMessage<ObjcObject>(
        getClass("NSString"),
        "stringWithUTF8String:",
        "C++ macOS Window with Image"
    );
    sendMessage<void>(window, "setTitle:", titleString);
    
    // Create and set window delegate to handle close events
    ObjcClass delegateClass = createWindowDelegateClass();
    ObjcObject delegate = sendClassMessage<ObjcObject>(delegateClass, "alloc");
    delegate = sendMessage<ObjcObject>(delegate, "init");
    sendMessage<void>(window, "setDelegate:", delegate);
    
    // Get the current content view bounds
    ObjcObject contentView = sendMessage<ObjcObject>(window, "contentView");
    NSRect contentBounds = sendMessage<NSRect>(contentView, "bounds");
    
    // Instead of creating a separate custom view, let's subclass the content view
    ObjcClass contentViewClass = createContentViewClass();
    
    // Replace the content view
    ObjcObject newContentView = sendClassMessage<ObjcObject>(contentViewClass, "alloc");
    newContentView = sendMessage<ObjcObject>(newContentView, "initWithFrame:", contentBounds);
    sendMessage<void>(window, "setContentView:", newContentView);
    sendMessage<void>(newContentView, "setNeedsDisplay:", YES);
    
    // Show window and run application
    sendMessage<void>(window, "makeKeyAndOrderFront:", nullptr);
    sendMessage<void>(application, "run");
    
    return 0;
}
