// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/debug/debugger.h"
#include "base/mac/scoped_nsobject.h"
#include "chrome/app/chrome_command_ids.h"
#import "chrome/browser/ui/cocoa/browser_window_controller.h"
#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#import "chrome/browser/ui/cocoa/framed_browser_window.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"

class FramedBrowserWindowTest : public CocoaTest {
 public:
  virtual void SetUp() {
    CocoaTest::SetUp();
    // Create a window.
    window_ = [[FramedBrowserWindow alloc]
               initWithContentRect:NSMakeRect(0, 0, 800, 600)
                       hasTabStrip:YES];
    if (base::debug::BeingDebugged()) {
      [window_ orderFront:nil];
    } else {
      [window_ orderBack:nil];
    }
  }

  virtual void TearDown() {
    [window_ close];
    CocoaTest::TearDown();
  }

  // Returns a canonical snapshot of the window.
  NSData* WindowContentsAsTIFF() {
    [window_ display];

    NSView* frameView = [window_ contentView];
    while ([frameView superview]) {
      frameView = [frameView superview];
    }

    // Inset to mask off left and right edges which vary in HighDPI.
    NSRect bounds = NSInsetRect([frameView bounds], 4, 0);

    // On 10.6, the grippy changes appearance slightly when painted the second
    // time in a textured window. Since this test cares about the window title,
    // cut off the bottom of the window.
    bounds.size.height -= 40;
    bounds.origin.y += 40;

    [frameView lockFocus];
    base::scoped_nsobject<NSBitmapImageRep> bitmap(
        [[NSBitmapImageRep alloc] initWithFocusedViewRect:bounds]);
    [frameView unlockFocus];

    return [bitmap TIFFRepresentation];
  }

  FramedBrowserWindow* window_;
};

// Baseline test that the window creates, displays, closes, and
// releases.
TEST_F(FramedBrowserWindowTest, ShowAndClose) {
  [window_ display];
}

// Test that undocumented title-hiding API we're using does the job.
TEST_F(FramedBrowserWindowTest, DoesHideTitle) {
  // The -display calls are not strictly necessary, but they do
  // make it easier to see what's happening when debugging (without
  // them the changes are never flushed to the screen).

  [window_ setTitle:@""];
  [window_ display];
  NSData* emptyTitleData = WindowContentsAsTIFF();

  [window_ setTitle:@"This is a title"];
  [window_ display];
  NSData* thisTitleData = WindowContentsAsTIFF();

  // The default window with a title should look different from the
  // window with an empty title.
  EXPECT_FALSE([emptyTitleData isEqualToData:thisTitleData]);

  [window_ setShouldHideTitle:YES];
  [window_ setTitle:@""];
  [window_ display];
  [window_ setTitle:@"This is a title"];
  [window_ display];
  NSData* hiddenTitleData = WindowContentsAsTIFF();

  // With our magic setting, the window with a title should look the
  // same as the window with an empty title.
  EXPECT_TRUE([window_ _isTitleHidden]);
  EXPECT_TRUE([emptyTitleData isEqualToData:hiddenTitleData]);
}

// Test to make sure that our window widgets are in the right place.
TEST_F(FramedBrowserWindowTest, WindowWidgetLocation) {
  BOOL yes = YES;
  BOOL no = NO;

  // First without a tabstrip.
  [window_ close];
  window_ = [[FramedBrowserWindow alloc]
             initWithContentRect:NSMakeRect(0, 0, 800, 600)
                     hasTabStrip:NO];
  id controller = [OCMockObject mockForClass:[BrowserWindowController class]];
  [[[controller stub] andReturnValue:OCMOCK_VALUE(yes)]
      isKindOfClass:[BrowserWindowController class]];
  [[[controller expect] andReturnValue:OCMOCK_VALUE(no)] hasTabStrip];
  [[[controller expect] andReturnValue:OCMOCK_VALUE(yes)] hasTitleBar];
  [[[controller expect] andReturnValue:OCMOCK_VALUE(no)] isTabbedWindow];
  [window_ setWindowController:controller];

  NSView* closeBoxControl = [window_ standardWindowButton:NSWindowCloseButton];
  EXPECT_TRUE(closeBoxControl);
  NSRect closeBoxFrame = [closeBoxControl frame];
  NSRect windowBounds = [window_ frame];
  windowBounds = [[window_ contentView] convertRect:windowBounds fromView:nil];
  windowBounds.origin = NSZeroPoint;
  EXPECT_EQ(NSMaxY(closeBoxFrame),
            NSMaxY(windowBounds) -
                kFramedWindowButtonsWithoutTabStripOffsetFromTop);
  EXPECT_EQ(NSMinX(closeBoxFrame),
            kFramedWindowButtonsWithoutTabStripOffsetFromLeft);

  NSView* miniaturizeControl =
      [window_ standardWindowButton:NSWindowMiniaturizeButton];
  EXPECT_TRUE(miniaturizeControl);
  NSRect miniaturizeFrame = [miniaturizeControl frame];
  EXPECT_EQ(NSMaxY(miniaturizeFrame),
            NSMaxY(windowBounds) -
                kFramedWindowButtonsWithoutTabStripOffsetFromTop);
  EXPECT_EQ(NSMinX(miniaturizeFrame),
            NSMaxX(closeBoxFrame) + [window_ windowButtonsInterButtonSpacing]);
  [window_ setWindowController:nil];

  // Then with a tabstrip.
  [window_ close];
  window_ = [[FramedBrowserWindow alloc]
             initWithContentRect:NSMakeRect(0, 0, 800, 600)
                     hasTabStrip:YES];
  controller = [OCMockObject mockForClass:[BrowserWindowController class]];
  [[[controller stub] andReturnValue:OCMOCK_VALUE(yes)]
      isKindOfClass:[BrowserWindowController class]];
  [[[controller expect] andReturnValue:OCMOCK_VALUE(yes)] hasTabStrip];
  [[[controller expect] andReturnValue:OCMOCK_VALUE(no)] hasTitleBar];
  [[[controller expect] andReturnValue:OCMOCK_VALUE(yes)] isTabbedWindow];
  [window_ setWindowController:controller];

  closeBoxControl = [window_ standardWindowButton:NSWindowCloseButton];
  EXPECT_TRUE(closeBoxControl);
  closeBoxFrame = [closeBoxControl frame];
  windowBounds = [window_ frame];
  windowBounds = [[window_ contentView] convertRect:windowBounds fromView:nil];
  windowBounds.origin = NSZeroPoint;
  EXPECT_EQ(NSMaxY(closeBoxFrame),
            NSMaxY(windowBounds) -
                kFramedWindowButtonsWithTabStripOffsetFromTop);
  EXPECT_EQ(NSMinX(closeBoxFrame),
            kFramedWindowButtonsWithTabStripOffsetFromLeft);

  miniaturizeControl = [window_ standardWindowButton:NSWindowMiniaturizeButton];
  EXPECT_TRUE(miniaturizeControl);
  miniaturizeFrame = [miniaturizeControl frame];
  EXPECT_EQ(NSMaxY(miniaturizeFrame),
            NSMaxY(windowBounds) -
                kFramedWindowButtonsWithTabStripOffsetFromTop);
  EXPECT_EQ(NSMinX(miniaturizeFrame),
            NSMaxX(closeBoxFrame) + [window_ windowButtonsInterButtonSpacing]);
  [window_ setWindowController:nil];
}
