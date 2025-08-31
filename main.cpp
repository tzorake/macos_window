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
inline CGRect CGMakeRect(double x, double y, double width, double height) { return { x, y, width, height }; }
inline CGPoint CGMakePoint(double x, double y) { return { x, y }; }
inline CGSize CGMakeSize(double width, double height) { return { width, height }; }

inline CGFloat CGRectGetWidth(CGRect rect) { return rect.size.width; }
inline CGFloat CGRectGetHeight(CGRect rect) { return rect.size.height; }

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

// Configuration constants
constexpr int gImageWidth = 800;
constexpr int gImageHeight = 600;
constexpr int gTargetFps = 60;
constexpr double gTargetFrameTime = 1.0 / gTargetFps;

// Global image data with mutex for thread safety
std::vector<std::uint32_t> gImageData;
std::mutex gImageDataMutex;
ObjcObject gContentView = nullptr;

// The windowShouldClose method implementation
bool windowShouldClose(ObjcObject self, ObjcSelector _cmd, ObjcObject sender)
{
    ObjcObject application = sendClassMessage<ObjcObject>(getClass("NSApplication"), "sharedApplication");
    sendMessage<void>(application, "terminate:", nullptr);
    return YES;
}

// Custom view drawRect method
void drawRect(ObjcObject self, ObjcSelector _cmd, CGRect rect)
{
    std::lock_guard<std::mutex> lock(gImageDataMutex);

    if (gImageData.empty())
        return;

    // Get view bounds
    CGRect bounds = sendMessage<CGRect>(self, "bounds");
    
    // Get graphics context
    ObjcObject context = sendClassMessage<ObjcObject>(getClass("NSGraphicsContext"), "currentContext");
    ObjcObject cgContext = sendMessage<ObjcObject>(context, "CGContext");
    
    // Cast to CGContextRef
    CGContextRef contextRef = reinterpret_cast<CGContextRef>(cgContext);
    
    // Draw the image using Core Graphics
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    
    CGContextSaveGState(contextRef);
    
    // Flip the coordinate system (macOS has origin at bottom-left)
    CGContextTranslateCTM(contextRef, 0, CGRectGetHeight(bounds));
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
        CGRect imageRect = CGRectMake(0, 0, CGRectGetWidth(bounds), CGRectGetHeight(bounds));
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

// Function to update image data dynamically
void updateImageData(const std::vector<std::uint32_t>& newData)
{
    if (newData.size() != gImageWidth * gImageHeight)
        return;
    
    {
        std::lock_guard<std::mutex> lock(gImageDataMutex);
        gImageData = newData;
    }
    
    // Request redraw on the main thread
    if (gContentView) {
        // Use performSelectorOnMainThread to ensure UI updates happen on the main thread
        ObjcSelector setNeedsDisplaySel = sel_registerName("setNeedsDisplay:");
        sendMessage<void>(
            gContentView, 
            "performSelectorOnMainThread:withObject:waitUntilDone:", 
            setNeedsDisplaySel, 
            sendClassMessage<ObjcObject>(getClass("NSNumber"), "numberWithBool:", YES), 
            YES
        );
    }
}

// Function to generate a simple animation frame
void generateAnimationFrame(std::size_t frameId)
{
    std::vector<std::uint32_t> newData(gImageWidth * gImageHeight);
    for (int y = 0; y < gImageHeight; ++y) {
        for (int x = 0; x < gImageWidth; ++x) {
            double timeFactor = frameId * gTargetFrameTime;
            std::uint8_t r = static_cast<std::uint8_t>((cos((double)x / gImageWidth + timeFactor) * 0.5 + 0.5) * 255);
            std::uint8_t g = static_cast<std::uint8_t>((sin((double)y / gImageHeight + timeFactor) * 0.5 + 0.5) * 255);
            std::uint8_t b = static_cast<std::uint8_t>((cos((double)(x + y) / (gImageWidth + gImageHeight) + timeFactor) * 0.5 + 0.5) * 255);
            std::uint8_t a = 255;

            // ARGB format (macOS expects premultiplied alpha)
            newData[y * gImageWidth + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    updateImageData(newData);
}

// Timer callback for animation
void timerCallback(CFRunLoopTimerRef timer, void* info)
{
    static std::size_t frameId = 0;
    generateAnimationFrame(frameId++);
}

int main()
{
    // Get shared application
    ObjcObject application = sendClassMessage<ObjcObject>(getClass("NSApplication"), "sharedApplication");
    sendMessage<void>(application, "setActivationPolicy:", AppActivation::Regular);

    // Calculate window position (center of screen)
    ObjcObject mainScreen = sendClassMessage<ObjcObject>(getClass("NSScreen"), "mainScreen");
    CGRect screenFrame = sendMessage<CGRect>(mainScreen, "frame");
    
    CGRect windowRect = CGMakeRect(
        (CGRectGetWidth(screenFrame) - gImageWidth) / 2, 
        (CGRectGetHeight(screenFrame) - gImageHeight) / 2, 
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
    CGRect contentBounds = sendMessage<CGRect>(contentView, "bounds");
    
    // Instead of creating a separate custom view, let's subclass the content view
    ObjcClass contentViewClass = createContentViewClass();
    
    // Replace the content view
    ObjcObject newContentView = sendClassMessage<ObjcObject>(contentViewClass, "alloc");
    newContentView = sendMessage<ObjcObject>(newContentView, "initWithFrame:", contentBounds);
    sendMessage<void>(window, "setContentView:", newContentView);
    sendMessage<void>(newContentView, "setNeedsDisplay:", YES);
    
    // Store the content view reference for dynamic updates
    gContentView = newContentView;
    
    // Set up a timer for animation demonstration using the target FPS
    CFRunLoopTimerContext timerContext = {0};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + gTargetFrameTime,
        gTargetFrameTime,
        0,
        0,
        timerCallback,
        &timerContext
    );
    CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);

    // Show window and run application
    sendMessage<void>(window, "makeKeyAndOrderFront:", nullptr);
    sendMessage<void>(application, "run");

    // Clean up
    CFRunLoopTimerInvalidate(timer);
    CFRelease(timer);
    
    return 0;
}
