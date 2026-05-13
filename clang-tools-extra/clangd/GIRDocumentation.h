//===--- GIRDocumentation.h - GIR-based documentation lookup ----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides documentation lookup from GObject Introspection Repository (.gir)
// files. GIR files contain rich API documentation for GLib/GTK libraries in
// XML format, mapping C symbol names to their documentation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_GIRDOCUMENTATION_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_GIRDOCUMENTATION_H

#include "Feature.h"

#if CLANGD_ENABLE_LIBXML2

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace clang {
namespace clangd {

class GIRDocumentationProvider {
public:
  /// Look up documentation for a C symbol by its identifier name.
  /// Returns the raw doc text (in GTK-Doc markup) if found.
  std::optional<std::string> lookup(llvm::StringRef SymbolName) const;

  /// Load and parse GIR files from the given paths.
  /// Each path can be a .gir file or a directory containing .gir files.
  void loadFromPaths(llvm::ArrayRef<std::string> Paths);

  /// Load and parse GIR files from default system paths
  /// (/usr/share/gir-1.0/ and XDG_DATA_DIRS).
  void loadFromDefaultPaths();

  /// Returns true if any GIR files have been loaded.
  bool isLoaded() const;

private:
  void parseGIRFile(llvm::StringRef Path);
  void loadDirectory(llvm::StringRef DirPath);

  mutable std::mutex Mu;
  bool Loaded = false;
  llvm::StringMap<std::string> SymbolDocs;
};

} // namespace clangd
} // namespace clang

#endif // CLANGD_ENABLE_LIBXML2
#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_GIRDOCUMENTATION_H
