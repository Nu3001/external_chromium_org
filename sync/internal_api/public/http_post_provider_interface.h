// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_INTERNAL_API_PUBLIC_HTTP_POST_PROVIDER_INTERFACE_H_
#define SYNC_INTERNAL_API_PUBLIC_HTTP_POST_PROVIDER_INTERFACE_H_

#include <string>

#include "sync/base/sync_export.h"

namespace syncer {

// An interface the embedding application (e.g. Chromium) implements to provide
// required HTTP POST functionality to the syncer backend. This interface is
// designed for one-time use. You create one, use it, and create another if you
// want to make a subsequent POST.
class SYNC_EXPORT_PRIVATE HttpPostProviderInterface {
 public:
  // Add additional headers to the request.
  virtual void SetExtraRequestHeaders(const char* headers) = 0;

  // Set the URL to POST to.
  virtual void SetURL(const char* url, int port) = 0;

  // Set the type, length and content of the POST payload.
  // |content_type| is a null-terminated MIME type specifier.
  // |content| is a data buffer; Do not interpret as a null-terminated string.
  // |content_length| is the total number of chars in |content|. It is used to
  // assign/copy |content| data.
  virtual void SetPostPayload(const char* content_type,
                              int content_length,
                              const char* content) = 0;

  // Returns true if the URL request succeeded. If the request failed,
  // error() may be non-zero and hence contain more information.
  virtual bool MakeSynchronousPost(int* error_code, int* response_code) = 0;

  // Get the length of the content returned in the HTTP response.
  // This does not count the trailing null-terminating character returned
  // by GetResponseContent, so it is analogous to calling string.length.
  virtual int GetResponseContentLength() const = 0;

  // Get the content returned in the HTTP response.
  // This is a null terminated string of characters.
  // Value should be copied.
  virtual const char* GetResponseContent() const = 0;

  // Get the value of a header returned in the HTTP response.
  // If the header is not present, returns the empty string.
  virtual const std::string GetResponseHeaderValue(
      const std::string& name) const = 0;

  // Abandon any pending POST and unblock caller in MakeSynchronousPost.
  // This must be safe to call from any thread.
  virtual void Abort() = 0;

 protected:
  virtual ~HttpPostProviderInterface() {}
};

}  // namespace syncer

#endif  // SYNC_INTERNAL_API_PUBLIC_HTTP_POST_PROVIDER_INTERFACE_H_
