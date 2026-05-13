//===-- GIRDocumentationTests.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Feature.h"

#if CLANGD_ENABLE_LIBXML2

#include "GIRDocumentation.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

namespace clang {
namespace clangd {
namespace {

class GIRDocumentationTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory("gir-test", TempDir));
  }

  void TearDown() override {
    llvm::sys::fs::remove_directories(TempDir);
  }

  void writeGIRFile(llvm::StringRef Name, llvm::StringRef Content) {
    llvm::SmallString<256> Path(TempDir);
    llvm::sys::path::append(Path, Name);
    std::error_code EC;
    llvm::raw_fd_ostream OS(Path, EC);
    ASSERT_FALSE(EC);
    OS << Content;
  }

  llvm::SmallString<256> TempDir;
};

constexpr const char *MinimalGIR = R"xml(<?xml version="1.0"?>
<repository version="1.2"
            xmlns="http://www.gtk.org/introspection/core/1.0"
            xmlns:c="http://www.gtk.org/introspection/c/1.0">
  <namespace name="Test" version="1.0"
             c:identifier-prefixes="Test"
             c:symbol-prefixes="test">
    <function name="do_something"
              c:identifier="test_do_something"
              version="1.2">
      <doc xml:space="preserve">Does something interesting.

This is the detailed description.</doc>
      <return-value transfer-ownership="none">
        <doc xml:space="preserve">zero on success, -1 on error</doc>
        <type name="gint" c:type="int"/>
      </return-value>
      <parameters>
        <parameter name="value" transfer-ownership="none">
          <doc xml:space="preserve">the value to process</doc>
          <type name="gint" c:type="int"/>
        </parameter>
      </parameters>
    </function>

    <function name="lookup"
              c:identifier="test_lookup"
              version="1.4">
      <doc xml:space="preserve">Looks up an item by key.</doc>
      <return-value transfer-ownership="full" nullable="1">
        <doc xml:space="preserve">the found item, or %NULL</doc>
        <type name="gpointer" c:type="gpointer"/>
      </return-value>
      <parameters>
        <parameter name="table" transfer-ownership="none">
          <doc xml:space="preserve">the table to search</doc>
          <type name="gpointer" c:type="gpointer"/>
        </parameter>
        <parameter name="key"
                   transfer-ownership="none"
                   nullable="1"
                   allow-none="1">
          <doc xml:space="preserve">the key to look up</doc>
          <type name="gpointer" c:type="gconstpointer"/>
        </parameter>
      </parameters>
    </function>

    <function name="old_func"
              c:identifier="test_old_func"
              version="0.8"
              deprecated="1"
              deprecated-version="1.6">
      <doc xml:space="preserve">Does something old.</doc>
      <return-value transfer-ownership="none">
        <type name="none" c:type="void"/>
      </return-value>
    </function>

    <function-macro name="IS_OBJECT"
                    c:identifier="TEST_IS_OBJECT">
      <doc xml:space="preserve">Checks whether @obj is a TestObject.</doc>
      <parameters>
        <parameter name="obj">
          <doc xml:space="preserve">a pointer to check</doc>
        </parameter>
      </parameters>
    </function-macro>

    <constant name="VERSION_MAJOR"
              value="2"
              c:type="TEST_VERSION_MAJOR">
      <doc xml:space="preserve">The major version number.</doc>
      <type name="gint" c:type="gint"/>
    </constant>

    <record name="Buffer"
            c:type="TestBuffer"
            c:symbol-prefix="buffer">
      <doc xml:space="preserve">A buffer for holding data.</doc>
      <field name="data" writable="1">
        <type name="utf8" c:type="gchar*"/>
      </field>
      <function name="new"
                c:identifier="test_buffer_new">
        <doc xml:space="preserve">Creates a new buffer.</doc>
        <return-value transfer-ownership="full">
          <doc xml:space="preserve">a new buffer</doc>
          <type name="Buffer" c:type="TestBuffer*"/>
        </return-value>
      </function>
      <method name="append"
              c:identifier="test_buffer_append">
        <doc xml:space="preserve">Appends data to the buffer.</doc>
        <return-value transfer-ownership="none">
          <type name="none" c:type="void"/>
        </return-value>
        <parameters>
          <instance-parameter name="self" transfer-ownership="none">
            <doc xml:space="preserve">the buffer</doc>
            <type name="Buffer" c:type="TestBuffer*"/>
          </instance-parameter>
          <parameter name="data" transfer-ownership="none">
            <doc xml:space="preserve">data to append</doc>
            <type name="utf8" c:type="const gchar*"/>
          </parameter>
        </parameters>
      </method>
    </record>

    <enumeration name="ErrorCode"
                 c:type="TestErrorCode">
      <doc xml:space="preserve">Error codes for test operations.</doc>
      <member name="none"
              value="0"
              c:identifier="TEST_ERROR_NONE">
        <doc xml:space="preserve">No error occurred.</doc>
      </member>
      <member name="failed"
              value="1"
              c:identifier="TEST_ERROR_FAILED">
        <doc xml:space="preserve">The operation failed.</doc>
      </member>
    </enumeration>

    <class name="Object"
           c:type="TestObject"
           c:symbol-prefix="object"
           version="1.0">
      <doc xml:space="preserve">Base object class.</doc>
      <constructor name="new"
                   c:identifier="test_object_new">
        <doc xml:space="preserve">Creates a new object.</doc>
        <return-value transfer-ownership="full">
          <doc xml:space="preserve">a new object</doc>
          <type name="Object" c:type="TestObject*"/>
        </return-value>
      </constructor>
      <method name="get_name"
              c:identifier="test_object_get_name">
        <doc xml:space="preserve">Gets the object name.</doc>
        <return-value transfer-ownership="none">
          <doc xml:space="preserve">the name</doc>
          <type name="utf8" c:type="const gchar*"/>
        </return-value>
        <parameters>
          <instance-parameter name="self" transfer-ownership="none">
            <doc xml:space="preserve">the object</doc>
            <type name="Object" c:type="TestObject*"/>
          </instance-parameter>
        </parameters>
      </method>
    </class>

    <callback name="Callback"
              c:type="TestCallback">
      <doc xml:space="preserve">A callback function type.</doc>
      <return-value transfer-ownership="none">
        <type name="none" c:type="void"/>
      </return-value>
      <parameters>
        <parameter name="data"
                   transfer-ownership="none"
                   nullable="1">
          <doc xml:space="preserve">user data</doc>
          <type name="gpointer" c:type="gpointer"/>
        </parameter>
      </parameters>
    </callback>

    <alias name="Handle"
           c:type="TestHandle">
      <doc xml:space="preserve">An opaque handle type.</doc>
      <type name="gpointer" c:type="gpointer"/>
    </alias>

    <bitfield name="Flags"
              c:type="TestFlags">
      <doc xml:space="preserve">Flags for test operations.</doc>
      <member name="none"
              value="0"
              c:identifier="TEST_FLAGS_NONE">
        <doc xml:space="preserve">No flags set.</doc>
      </member>
    </bitfield>
  </namespace>
</repository>
)xml";

TEST_F(GIRDocumentationTest, FunctionWithParamsAndReturn) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("test_do_something");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Does something interesting."), std::string::npos);
  EXPECT_NE(Doc->find("detailed description"), std::string::npos);
  EXPECT_NE(Doc->find("@value:"), std::string::npos);
  EXPECT_NE(Doc->find("the value to process"), std::string::npos);
  EXPECT_NE(Doc->find("Returns:"), std::string::npos);
  EXPECT_NE(Doc->find("zero on success"), std::string::npos);
  EXPECT_NE(Doc->find("Since: 1.2"), std::string::npos);
}

TEST_F(GIRDocumentationTest, FunctionWithNullableAnnotations) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("test_lookup");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Looks up an item"), std::string::npos);
  // Parameter with nullable annotation.
  EXPECT_NE(Doc->find("@key:"), std::string::npos);
  EXPECT_NE(Doc->find("(nullable)"), std::string::npos);
  EXPECT_NE(Doc->find("the key to look up"), std::string::npos);
  // Return value with transfer-ownership and nullable.
  EXPECT_NE(Doc->find("Returns:"), std::string::npos);
  EXPECT_NE(Doc->find("(transfer full)"), std::string::npos);
  EXPECT_NE(Doc->find("the found item"), std::string::npos);
  EXPECT_NE(Doc->find("Since: 1.4"), std::string::npos);
}

TEST_F(GIRDocumentationTest, DeprecatedFunction) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("test_old_func");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Does something old"), std::string::npos);
  EXPECT_NE(Doc->find("Since: 0.8"), std::string::npos);
  EXPECT_NE(Doc->find("Deprecated:"), std::string::npos);
  EXPECT_NE(Doc->find("1.6"), std::string::npos);
}

TEST_F(GIRDocumentationTest, FunctionMacroWithParams) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TEST_IS_OBJECT");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Checks whether"), std::string::npos);
  EXPECT_NE(Doc->find("@obj:"), std::string::npos);
  EXPECT_NE(Doc->find("a pointer to check"), std::string::npos);
}

TEST_F(GIRDocumentationTest, Constant) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TEST_VERSION_MAJOR");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("major version"), std::string::npos);
}

TEST_F(GIRDocumentationTest, Record) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TestBuffer");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("buffer for holding data"), std::string::npos);
}

TEST_F(GIRDocumentationTest, RecordMethodWithInstanceParam) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("test_buffer_append");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Appends data"), std::string::npos);
  // instance-parameter is included.
  EXPECT_NE(Doc->find("@self:"), std::string::npos);
  EXPECT_NE(Doc->find("@data:"), std::string::npos);
  EXPECT_NE(Doc->find("data to append"), std::string::npos);

  auto DocNew = Provider.lookup("test_buffer_new");
  ASSERT_TRUE(DocNew.has_value());
  EXPECT_NE(DocNew->find("Creates a new buffer"), std::string::npos);
  EXPECT_NE(DocNew->find("Returns:"), std::string::npos);
  EXPECT_NE(DocNew->find("(transfer full)"), std::string::npos);
}

TEST_F(GIRDocumentationTest, Enum) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TestErrorCode");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Error codes"), std::string::npos);

  auto MemberDoc = Provider.lookup("TEST_ERROR_NONE");
  ASSERT_TRUE(MemberDoc.has_value());
  EXPECT_NE(MemberDoc->find("No error"), std::string::npos);
}

TEST_F(GIRDocumentationTest, ClassWithMethodReturn) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TestObject");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Base object class"), std::string::npos);
  EXPECT_NE(Doc->find("Since: 1.0"), std::string::npos);

  auto CtorDoc = Provider.lookup("test_object_new");
  ASSERT_TRUE(CtorDoc.has_value());
  EXPECT_NE(CtorDoc->find("Creates a new object"), std::string::npos);
  EXPECT_NE(CtorDoc->find("Returns:"), std::string::npos);
  EXPECT_NE(CtorDoc->find("(transfer full)"), std::string::npos);

  auto MethodDoc = Provider.lookup("test_object_get_name");
  ASSERT_TRUE(MethodDoc.has_value());
  EXPECT_NE(MethodDoc->find("Gets the object name"), std::string::npos);
  EXPECT_NE(MethodDoc->find("@self:"), std::string::npos);
  EXPECT_NE(MethodDoc->find("Returns:"), std::string::npos);
  EXPECT_NE(MethodDoc->find("the name"), std::string::npos);
}

TEST_F(GIRDocumentationTest, CallbackWithNullableParam) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TestCallback");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("callback function type"), std::string::npos);
  EXPECT_NE(Doc->find("@data:"), std::string::npos);
  EXPECT_NE(Doc->find("(nullable)"), std::string::npos);
  EXPECT_NE(Doc->find("user data"), std::string::npos);
}

TEST_F(GIRDocumentationTest, Alias) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TestHandle");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("opaque handle type"), std::string::npos);
}

TEST_F(GIRDocumentationTest, Bitfield) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("TestFlags");
  ASSERT_TRUE(Doc.has_value());
  EXPECT_NE(Doc->find("Flags for test"), std::string::npos);

  auto MemberDoc = Provider.lookup("TEST_FLAGS_NONE");
  ASSERT_TRUE(MemberDoc.has_value());
  EXPECT_NE(MemberDoc->find("No flags set"), std::string::npos);
}

TEST_F(GIRDocumentationTest, LookupMissing) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  auto Doc = Provider.lookup("nonexistent_function");
  EXPECT_FALSE(Doc.has_value());
}

TEST_F(GIRDocumentationTest, LoadFromDirectory) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);
  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(TempDir)});

  EXPECT_TRUE(Provider.isLoaded());
  EXPECT_TRUE(Provider.lookup("test_do_something").has_value());
}

TEST_F(GIRDocumentationTest, LoadSpecificFile) {
  writeGIRFile("Test-1.0.gir", MinimalGIR);

  llvm::SmallString<256> FilePath(TempDir);
  llvm::sys::path::append(FilePath, "Test-1.0.gir");

  GIRDocumentationProvider Provider;
  Provider.loadFromPaths({std::string(FilePath)});

  EXPECT_TRUE(Provider.isLoaded());
  EXPECT_TRUE(Provider.lookup("test_do_something").has_value());
}

TEST_F(GIRDocumentationTest, EmptyProvider) {
  GIRDocumentationProvider Provider;
  EXPECT_FALSE(Provider.isLoaded());
  EXPECT_FALSE(Provider.lookup("anything").has_value());
}

} // namespace
} // namespace clangd
} // namespace clang

#endif // CLANGD_ENABLE_LIBXML2
