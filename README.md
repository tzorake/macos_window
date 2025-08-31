# macOS Window with Image Display

## Description

This project demonstrates how to create a native macOS application using C++ with direct Objective-C runtime calls. The application displays a procedurally generated gradient image in a custom window without using standard application frameworks.

https://github.com/user-attachments/assets/1a4e34b9-313b-4e57-8ed5-c15147114f76

## Building the Application

To compile this code, you need the macOS SDK and a C++ compiler with C++11 support. Use the following command with clang++:

```
clang++ --sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk -std=c++11 -framework Cocoa main.cpp -o app
```

This command compiles the source code, links against the necessary frameworks, and produces an executable.

After successful compilation, run the application with:

```
./app
```