// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/autofill/down_arrow_popup_menu_cell.h"

#include <algorithm>

#include "chrome/browser/ui/cocoa/autofill/autofill_dialog_constants.h"

@implementation DownArrowPopupMenuCell

- (NSSize)imageSize {
  image_button_cell::ButtonState state = image_button_cell::kDefaultState;
  NSView* controlView = [self controlView];
  NSImage* image = [self imageForState:state view:controlView];
  return [image size];
}

- (NSSize)cellSize {
  NSSize imageSize = [self imageSize];

  NSAttributedString* title = [self attributedTitle];
  NSSize size = [title size];
  size.height = std::max(size.height, imageSize.height);
  size.width += kButtonGap + imageSize.width;

  return size;
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView {
  NSRect imageRect, titleRect;
  NSDivideRect(
      cellFrame, &imageRect, &titleRect, [self imageSize].width, NSMaxXEdge);
  [super drawWithFrame:imageRect inView:controlView];

  NSAttributedString* title = [self attributedTitle];
  if ([title length])
    [self drawTitle:title withFrame:titleRect inView:controlView];
}

@end

