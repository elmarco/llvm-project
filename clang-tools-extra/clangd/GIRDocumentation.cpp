//===--- GIRDocumentation.cpp - GIR-based documentation lookup --*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GIRDocumentation.h"

#if CLANGD_ENABLE_LIBXML2

#include "support/Logger.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

namespace clang {
namespace clangd {
namespace {

constexpr const char *GIR_C_NS = "http://www.gtk.org/introspection/c/1.0";

llvm::StringRef xmlStr(const xmlChar *S) {
  return S ? llvm::StringRef(reinterpret_cast<const char *>(S)) : "";
}

struct XmlCharDeleter {
  void operator()(xmlChar *P) const { xmlFree(P); }
};
using XmlCharPtr = std::unique_ptr<xmlChar, XmlCharDeleter>;

std::string getAttr(xmlNodePtr Node, const char *Name) {
  XmlCharPtr V(xmlGetProp(Node, reinterpret_cast<const xmlChar *>(Name)));
  return V ? std::string(reinterpret_cast<const char *>(V.get())) : "";
}

std::string getNsAttr(xmlNodePtr Node, const char *Name, const char *NS) {
  XmlCharPtr V(xmlGetNsProp(Node, reinterpret_cast<const xmlChar *>(Name),
                            reinterpret_cast<const xmlChar *>(NS)));
  return V ? std::string(reinterpret_cast<const char *>(V.get())) : "";
}

std::string getCIdentifier(xmlNodePtr Node) {
  std::string Id = getNsAttr(Node, "identifier", GIR_C_NS);
  if (!Id.empty())
    return Id;
  return getNsAttr(Node, "type", GIR_C_NS);
}

xmlNodePtr findChild(xmlNodePtr Parent, const char *Name) {
  for (xmlNodePtr C = Parent->children; C; C = C->next)
    if (C->type == XML_ELEMENT_NODE && xmlStr(C->name) == Name)
      return C;
  return nullptr;
}

std::string getNodeText(xmlNodePtr Node) {
  if (!Node)
    return {};
  XmlCharPtr Content(xmlNodeGetContent(Node));
  return Content ? std::string(reinterpret_cast<const char *>(Content.get()))
                 : "";
}

std::string getDocText(xmlNodePtr Node) {
  if (!Node)
    return {};
  return getNodeText(findChild(Node, "doc"));
}

bool isDocumentableElement(llvm::StringRef Name) {
  return Name == "function" || Name == "function-macro" ||
         Name == "method" || Name == "constructor" ||
         Name == "constant" || Name == "record" ||
         Name == "class" || Name == "interface" ||
         Name == "enumeration" || Name == "bitfield" ||
         Name == "member" || Name == "callback" ||
         Name == "alias" || Name == "virtual-method";
}

bool isCallableElement(llvm::StringRef Name) {
  return Name == "function" || Name == "function-macro" ||
         Name == "method" || Name == "constructor" ||
         Name == "callback" || Name == "virtual-method";
}

// Build annotations string like "(transfer full) (nullable)".
std::string buildAnnotations(xmlNodePtr Node) {
  std::string Annots;
  std::string Transfer = getAttr(Node, "transfer-ownership");
  if (!Transfer.empty() && Transfer != "none") {
    if (!Annots.empty())
      Annots += ' ';
    Annots += "(transfer " + Transfer + ")";
  }
  if (getAttr(Node, "nullable") == "1" ||
      getAttr(Node, "allow-none") == "1") {
    if (!Annots.empty())
      Annots += ' ';
    Annots += "(nullable)";
  }
  if (getAttr(Node, "optional") == "1") {
    if (!Annots.empty())
      Annots += ' ';
    Annots += "(optional)";
  }
  return Annots;
}

// Reconstruct GTK-Doc formatted text from a GIR element's structured data.
std::string buildDocumentation(xmlNodePtr Node) {
  llvm::StringRef ElemName = xmlStr(Node->name);
  std::string Doc;

  // Main description from the element's <doc>.
  std::string Desc = getDocText(Node);
  if (!Desc.empty())
    Doc += Desc;

  // For callable elements, extract parameters and return value.
  if (isCallableElement(ElemName)) {
    // Parameters.
    xmlNodePtr ParamsNode = findChild(Node, "parameters");
    if (ParamsNode) {
      for (xmlNodePtr P = ParamsNode->children; P; P = P->next) {
        if (P->type != XML_ELEMENT_NODE)
          continue;
        llvm::StringRef PName = xmlStr(P->name);
        if (PName != "parameter" && PName != "instance-parameter")
          continue;

        std::string ParamName = getAttr(P, "name");
        if (ParamName.empty())
          continue;

        std::string ParamDoc = getDocText(P);
        std::string Annots = buildAnnotations(P);

        if (!ParamDoc.empty() || !Annots.empty()) {
          Doc += "\n@";
          Doc += ParamName;
          Doc += ':';
          if (!Annots.empty()) {
            Doc += ' ';
            Doc += Annots;
            Doc += ':';
          }
          if (!ParamDoc.empty()) {
            Doc += ' ';
            Doc += ParamDoc;
          }
        }
      }
    }

    // Return value.
    xmlNodePtr RetNode = findChild(Node, "return-value");
    if (RetNode) {
      std::string RetDoc = getDocText(RetNode);
      std::string RetAnnots = buildAnnotations(RetNode);
      if (!RetDoc.empty() || !RetAnnots.empty()) {
        Doc += "\n\nReturns:";
        if (!RetAnnots.empty()) {
          Doc += ' ';
          Doc += RetAnnots;
          Doc += ':';
        }
        if (!RetDoc.empty()) {
          Doc += ' ';
          Doc += RetDoc;
        }
      }
    }
  }

  // Metadata from attributes.
  std::string Version = getAttr(Node, "version");
  if (!Version.empty()) {
    Doc += "\n\nSince: ";
    Doc += Version;
  }

  if (getAttr(Node, "deprecated") == "1") {
    std::string DepVer = getAttr(Node, "deprecated-version");
    std::string DepDoc = getNodeText(findChild(Node, "doc-deprecated"));
    Doc += "\n\nDeprecated:";
    if (!DepVer.empty()) {
      Doc += ' ';
      Doc += DepVer;
      Doc += '.';
    }
    if (!DepDoc.empty()) {
      Doc += ' ';
      Doc += DepDoc;
    }
  }

  return Doc;
}

void processElement(xmlNodePtr Node, llvm::StringMap<std::string> &SymbolDocs) {
  if (Node->type != XML_ELEMENT_NODE)
    return;

  llvm::StringRef ElemName = xmlStr(Node->name);

  if (isDocumentableElement(ElemName)) {
    std::string Id = getCIdentifier(Node);
    if (!Id.empty()) {
      std::string Doc = buildDocumentation(Node);
      if (!Doc.empty())
        SymbolDocs.try_emplace(std::move(Id), std::move(Doc));
    }
  }

  // Recurse into children to find nested documentable elements
  // (methods inside records/classes, members inside enumerations, etc.)
  for (xmlNodePtr C = Node->children; C; C = C->next)
    processElement(C, SymbolDocs);
}

} // namespace

void GIRDocumentationProvider::parseGIRFile(llvm::StringRef Path) {
  xmlDocPtr XmlDoc =
      xmlReadFile(Path.str().c_str(), nullptr, XML_PARSE_NONET);
  if (!XmlDoc) {
    elog("GIR: failed to parse {0}", Path);
    return;
  }

  xmlNodePtr Root = xmlDocGetRootElement(XmlDoc);
  if (Root)
    processElement(Root, SymbolDocs);

  xmlFreeDoc(XmlDoc);
}

void GIRDocumentationProvider::loadDirectory(llvm::StringRef DirPath) {
  std::error_code EC;
  for (llvm::sys::fs::directory_iterator DI(DirPath, EC), DE;
       DI != DE && !EC; DI.increment(EC)) {
    llvm::StringRef FilePath = DI->path();
    if (FilePath.ends_with(".gir"))
      parseGIRFile(FilePath);
  }
  if (EC)
    elog("GIR: error scanning directory {0}: {1}", DirPath, EC.message());
}

void GIRDocumentationProvider::loadFromPaths(
    llvm::ArrayRef<std::string> Paths) {
  std::lock_guard<std::mutex> Lock(Mu);
  for (const auto &P : Paths) {
    llvm::sys::fs::file_status Status;
    if (llvm::sys::fs::status(P, Status))
      continue;
    if (llvm::sys::fs::is_directory(Status))
      loadDirectory(P);
    else if (llvm::StringRef(P).ends_with(".gir"))
      parseGIRFile(P);
  }
  Loaded = true;
  log("GIR: loaded documentation for {0} symbols", SymbolDocs.size());
}

void GIRDocumentationProvider::loadFromDefaultPaths() {
  std::vector<std::string> Paths;

  // Standard system path.
  Paths.push_back("/usr/share/gir-1.0");

  // Also check XDG_DATA_DIRS.
  if (const char *XDGDirs = ::getenv("XDG_DATA_DIRS")) {
    llvm::SmallVector<llvm::StringRef> Dirs;
    llvm::StringRef(XDGDirs).split(Dirs, ':');
    for (auto Dir : Dirs) {
      llvm::SmallString<256> GIRDir(Dir);
      llvm::sys::path::append(GIRDir, "gir-1.0");
      if (llvm::sys::fs::is_directory(GIRDir))
        Paths.push_back(std::string(GIRDir));
    }
  }

  loadFromPaths(Paths);
}

std::optional<std::string>
GIRDocumentationProvider::lookup(llvm::StringRef SymbolName) const {
  std::lock_guard<std::mutex> Lock(Mu);
  auto It = SymbolDocs.find(SymbolName);
  if (It == SymbolDocs.end())
    return std::nullopt;
  return It->second;
}

bool GIRDocumentationProvider::isLoaded() const {
  std::lock_guard<std::mutex> Lock(Mu);
  return Loaded;
}

} // namespace clangd
} // namespace clang

#endif // CLANGD_ENABLE_LIBXML2
