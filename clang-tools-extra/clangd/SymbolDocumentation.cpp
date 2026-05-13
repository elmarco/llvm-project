//===--- SymbolDocumentation.cpp ==-------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolDocumentation.h"

#include "support/Markup.h"
#include "clang/AST/Comment.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/CommentVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace clangd {
namespace {

std::string commandMarkerAsString(comments::CommandMarkerKind CommandMarker) {
  switch (CommandMarker) {
  case comments::CommandMarkerKind::CMK_At:
    return "@";
  case comments::CommandMarkerKind::CMK_Backslash:
    return "\\";
  }
  llvm_unreachable("Unknown command marker kind");
}

void commandToMarkup(markup::Paragraph &Out, StringRef Command,
                     comments::CommandMarkerKind CommandMarker,
                     StringRef Args) {
  Out.appendBoldText(commandMarkerAsString(CommandMarker) + Command.str());
  Out.appendSpace();
  if (!Args.empty())
    Out.appendCode(Args.str());
}

template <typename T> std::string getArgText(const T *Command) {
  std::string ArgText;
  for (unsigned I = 0; I < Command->getNumArgs(); ++I) {
    if (!ArgText.empty())
      ArgText += " ";
    ArgText += Command->getArgText(I);
  }
  return ArgText;
}

} // namespace

class ParagraphToMarkupDocument
    : public comments::ConstCommentVisitor<ParagraphToMarkupDocument> {
public:
  ParagraphToMarkupDocument(markup::Paragraph &Out,
                            const comments::CommandTraits &Traits)
      : Out(Out), Traits(Traits) {}

  void visitParagraphComment(const comments::ParagraphComment *C) {
    if (!C)
      return;

    for (const auto *Child = C->child_begin(); Child != C->child_end();
         ++Child) {
      visit(*Child);
    }
  }

  void visitTextComment(const comments::TextComment *C) {
    // Always trim leading space after a newline.
    StringRef Text = C->getText();
    if (LastChunkEndsWithNewline && C->getText().starts_with(' '))
      Text = Text.drop_front();

    LastChunkEndsWithNewline = C->hasTrailingNewline();
    Out.appendText(Text.str() + (LastChunkEndsWithNewline ? "\n" : ""));
  }

  void visitInlineCommandComment(const comments::InlineCommandComment *C) {

    if (C->getNumArgs() > 0) {
      std::string ArgText = getArgText(C);

      switch (C->getRenderKind()) {
      case comments::InlineCommandRenderKind::Monospaced:
        Out.appendCode(ArgText);
        break;
      case comments::InlineCommandRenderKind::Bold:
        Out.appendBoldText(ArgText);
        break;
      case comments::InlineCommandRenderKind::Emphasized:
        Out.appendEmphasizedText(ArgText);
        break;
      default:
        commandToMarkup(Out, C->getCommandName(Traits), C->getCommandMarker(),
                        ArgText);
        break;
      }
    } else {
      if (C->getCommandName(Traits) == "n") {
        // \n is a special case, it is used to create a new line.
        Out.appendText("  \n");
        LastChunkEndsWithNewline = true;
        return;
      }

      commandToMarkup(Out, C->getCommandName(Traits), C->getCommandMarker(),
                      "");
    }
  }

  void visitHTMLStartTagComment(const comments::HTMLStartTagComment *STC) {
    std::string TagText = "<" + STC->getTagName().str();

    for (unsigned I = 0; I < STC->getNumAttrs(); ++I) {
      const comments::HTMLStartTagComment::Attribute &Attr = STC->getAttr(I);
      TagText += " " + Attr.Name.str() + "=\"" + Attr.Value.str() + "\"";
    }

    if (STC->isSelfClosing())
      TagText += " /";
    TagText += ">";

    LastChunkEndsWithNewline = STC->hasTrailingNewline();
    Out.appendText(TagText + (LastChunkEndsWithNewline ? "\n" : ""));
  }

  void visitHTMLEndTagComment(const comments::HTMLEndTagComment *ETC) {
    LastChunkEndsWithNewline = ETC->hasTrailingNewline();
    Out.appendText("</" + ETC->getTagName().str() + ">" +
                   (LastChunkEndsWithNewline ? "\n" : ""));
  }

private:
  markup::Paragraph &Out;
  const comments::CommandTraits &Traits;

  /// If true, the next leading space after a new line is trimmed.
  /// Initially set it to true, to always trim the first text line.
  bool LastChunkEndsWithNewline = true;
};

class ParagraphToString
    : public comments::ConstCommentVisitor<ParagraphToString> {
public:
  ParagraphToString(llvm::raw_string_ostream &Out,
                    const comments::CommandTraits &Traits)
      : Out(Out), Traits(Traits) {}

  void visitParagraphComment(const comments::ParagraphComment *C) {
    if (!C)
      return;

    for (const auto *Child = C->child_begin(); Child != C->child_end();
         ++Child) {
      visit(*Child);
    }
  }

  void visitTextComment(const comments::TextComment *C) { Out << C->getText(); }

  void visitInlineCommandComment(const comments::InlineCommandComment *C) {
    Out << commandMarkerAsString(C->getCommandMarker());
    Out << C->getCommandName(Traits);
    std::string ArgText = getArgText(C);
    if (!ArgText.empty())
      Out << " " << ArgText;
    Out << " ";
  }

  void visitHTMLStartTagComment(const comments::HTMLStartTagComment *STC) {
    Out << "<" << STC->getTagName().str();

    for (unsigned I = 0; I < STC->getNumAttrs(); ++I) {
      const comments::HTMLStartTagComment::Attribute &Attr = STC->getAttr(I);
      Out << " " << Attr.Name.str();
      if (!Attr.Value.str().empty())
        Out << "=\"" << Attr.Value.str() << "\"";
    }

    if (STC->isSelfClosing())
      Out << " /";
    Out << ">";

    Out << (STC->hasTrailingNewline() ? "\n" : "");
  }

  void visitHTMLEndTagComment(const comments::HTMLEndTagComment *ETC) {
    Out << "</" << ETC->getTagName().str() << ">"
        << (ETC->hasTrailingNewline() ? "\n" : "");
  }

private:
  llvm::raw_string_ostream &Out;
  const comments::CommandTraits &Traits;
};

class BlockCommentToMarkupDocument
    : public comments::ConstCommentVisitor<BlockCommentToMarkupDocument> {
public:
  BlockCommentToMarkupDocument(markup::Document &Out,
                               const comments::CommandTraits &Traits)
      : Out(Out), Traits(Traits) {}

  void visitBlockCommandComment(const comments::BlockCommandComment *B) {

    switch (B->getCommandID()) {
    case comments::CommandTraits::KCI_arg:
    case comments::CommandTraits::KCI_li:
      // \li and \arg are special cases, they are used to create a list item.
      // In markdown it is a bullet list.
      ParagraphToMarkupDocument(Out.addBulletList().addItem().addParagraph(),
                                Traits)
          .visit(B->getParagraph());
      break;
    case comments::CommandTraits::KCI_note:
    case comments::CommandTraits::KCI_warning:
      commandToHeadedParagraph(B);
      break;
    case comments::CommandTraits::KCI_retval: {
      // The \retval command describes the return value given as its single
      // argument in the corresponding paragraph.
      // Note: We know that we have exactly one argument but not if it has an
      // associated paragraph.
      auto &P = Out.addParagraph().appendCode(getArgText(B));
      if (B->getParagraph() && !B->getParagraph()->isWhitespace()) {
        P.appendText(" - ");
        ParagraphToMarkupDocument(P, Traits).visit(B->getParagraph());
      }
      return;
    }
    case comments::CommandTraits::KCI_details: {
      // The \details command is just used to separate the brief from the
      // detailed description. This separation is already done in the
      // SymbolDocCommentVisitor. Therefore we can omit the command itself
      // here and just process the paragraph.
      if (B->getParagraph() && !B->getParagraph()->isWhitespace()) {
        ParagraphToMarkupDocument(Out.addParagraph(), Traits)
            .visit(B->getParagraph());
      }
      return;
    }
    default: {
      // Some commands have arguments, like \throws.
      // The arguments are not part of the paragraph.
      // We need reconstruct them here.
      std::string ArgText = getArgText(B);
      auto &P = Out.addParagraph();
      commandToMarkup(P, B->getCommandName(Traits), B->getCommandMarker(),
                      ArgText);
      if (B->getParagraph() && !B->getParagraph()->isWhitespace()) {
        // For commands with arguments, the paragraph starts after the first
        // space. Therefore we need to append a space manually in this case.
        if (!ArgText.empty())
          P.appendSpace();
        ParagraphToMarkupDocument(P, Traits).visit(B->getParagraph());
      }
    }
    }
  }

  void visitCodeCommand(const comments::VerbatimBlockComment *VB) {
    std::string CodeLang = "";
    auto *FirstLine = VB->child_begin();
    // The \\code command has an optional language argument.
    // This argument is currently not parsed by the clang doxygen parser.
    // Therefore we try to extract it from the first line of the verbatim
    // block.
    if (VB->getNumLines() > 0) {
      if (const auto *Line =
              cast<comments::VerbatimBlockLineComment>(*FirstLine)) {
        llvm::StringRef Text = Line->getText();
        // Language is a single word enclosed in {}.
        if (llvm::none_of(Text, llvm::isSpace) && Text.consume_front("{") &&
            Text.consume_back("}")) {
          // drop a potential . since this is not supported in Markdown
          // fenced code blocks.
          Text.consume_front(".");
          // Language is alphanumeric or '+'.
          CodeLang = Text.take_while([](char C) {
                           return llvm::isAlnum(C) || C == '+';
                         })
                         .str();
          // Skip the first line for the verbatim text.
          ++FirstLine;
        }
      }
    }

    std::string CodeBlockText;

    for (const auto *LI = FirstLine; LI != VB->child_end(); ++LI) {
      if (const auto *Line = cast<comments::VerbatimBlockLineComment>(*LI)) {
        CodeBlockText += Line->getText().str() + "\n";
      }
    }

    Out.addCodeBlock(CodeBlockText, CodeLang);
  }

  void visitVerbatimBlockComment(const comments::VerbatimBlockComment *VB) {
    // The \\code command is a special verbatim block command which we handle
    // separately.
    if (VB->getCommandID() == comments::CommandTraits::KCI_code) {
      visitCodeCommand(VB);
      return;
    }

    commandToMarkup(Out.addParagraph(), VB->getCommandName(Traits),
                    VB->getCommandMarker(), "");

    std::string VerbatimText;

    for (const auto *LI = VB->child_begin(); LI != VB->child_end(); ++LI) {
      if (const auto *Line = cast<comments::VerbatimBlockLineComment>(*LI)) {
        VerbatimText += Line->getText().str() + "\n";
      }
    }

    Out.addCodeBlock(VerbatimText, "");

    commandToMarkup(Out.addParagraph(), VB->getCloseName(),
                    VB->getCommandMarker(), "");
  }

  void visitVerbatimLineComment(const comments::VerbatimLineComment *VL) {
    auto &P = Out.addParagraph();
    commandToMarkup(P, VL->getCommandName(Traits), VL->getCommandMarker(), "");
    P.appendSpace().appendCode(VL->getText().str(), true).appendSpace();
  }

private:
  markup::Document &Out;
  const comments::CommandTraits &Traits;
  StringRef CommentEscapeMarker;

  /// Emphasize the given command in a paragraph.
  /// Uses the command name with the first letter capitalized as the heading.
  void commandToHeadedParagraph(const comments::BlockCommandComment *B) {
    auto &P = Out.addParagraph();
    std::string Heading = B->getCommandName(Traits).slice(0, 1).upper() +
                          B->getCommandName(Traits).drop_front().str();
    P.appendBoldText(Heading + ":");
    P.appendText("  \n");
    ParagraphToMarkupDocument(P, Traits).visit(B->getParagraph());
  }
};

void SymbolDocCommentVisitor::preprocessDocumentation(StringRef Doc) {
  enum State {
    Normal,
    FencedCodeblock,
  } State = Normal;
  std::string CodeFence;

  llvm::raw_string_ostream OS(CommentWithMarkers);

  // The documentation string is processed line by line.
  // The raw documentation string does not contain the comment markers
  // (e.g. /// or /** */).
  // But the comment lexer expects doxygen markers, so add them back.
  // We need to use the /// style doxygen markers because the comment could
  // contain the closing tag "*/" of a C Style "/** */" comment
  // which would break the parsing if we would just enclose the comment text
  // with "/** */".

  // Escape doxygen commands inside markdown inline code spans.
  // This is required to not let the doxygen parser interpret them as
  // commands.
  // Note: This is a heuristic which may fail in some cases.
  bool InCodeSpan = false;

  llvm::StringRef Line, Rest;
  for (std::tie(Line, Rest) = Doc.split('\n'); !(Line.empty() && Rest.empty());
       std::tie(Line, Rest) = Rest.split('\n')) {

    // Detect code fence (``` or ~~~)
    if (State == Normal) {
      llvm::StringRef Trimmed = Line.ltrim();
      if (Trimmed.starts_with("```") || Trimmed.starts_with("~~~")) {
        // https://www.doxygen.nl/manual/markdown.html#md_fenced
        CodeFence =
            Trimmed.take_while([](char C) { return C == '`' || C == '~'; })
                .str();
        // Try to detect language: first word after fence. Could also be
        // enclosed in {}
        llvm::StringRef AfterFence =
            Trimmed.drop_front(CodeFence.size()).ltrim();
        // ignore '{' at the beginning of the language name to not duplicate it
        // for the doxygen command
        AfterFence.consume_front("{");
        // The name is alphanumeric or '.' or '+'
        StringRef CodeLang = AfterFence.take_while(
            [](char C) { return llvm::isAlnum(C) || C == '.' || C == '+'; });

        OS << "///@code";

        if (!CodeLang.empty())
          OS << "{" << CodeLang.str() << "}";

        OS << "\n";

        State = FencedCodeblock;
        continue;
      }

      // FIXME: handle indented code blocks too?
      // In doxygen, the indentation which triggers a code block depends on the
      // indentation of the previous paragraph.
      // https://www.doxygen.nl/manual/markdown.html#mddox_code_blocks
    } else if (State == FencedCodeblock) {
      // End of code fence
      if (Line.ltrim().starts_with(CodeFence)) {
        OS << "///@endcode\n";
        State = Normal;
        continue;
      }
      OS << "///" << Line << "\n";
      continue;
    }

    // Normal line preprocessing (add doxygen markers, handle escaping)
    OS << "///";

    if (Line.empty() || Line.trim().empty()) {
      OS << "\n";
      // Empty lines reset the InCodeSpan state.
      InCodeSpan = false;
      continue;
    }

    if (Line.starts_with("<"))
      // A comment line starting with '///<' is treated as a doxygen
      // command. To avoid this, we add a space before the '<'.
      OS << ' ';

    for (char C : Line) {
      if (C == '`')
        InCodeSpan = !InCodeSpan;
      else if (InCodeSpan && (C == '@' || C == '\\'))
        OS << '\\';
      OS << C;
    }

    OS << "\n";
  }

  // Close any unclosed code block
  if (State == FencedCodeblock)
    OS << "///@endcode\n";
}

void SymbolDocCommentVisitor::visitBlockCommandComment(
    const comments::BlockCommandComment *B) {
  switch (B->getCommandID()) {
  case comments::CommandTraits::KCI_brief: {
    if (!BriefParagraph) {
      BriefParagraph = B->getParagraph();
      return;
    }
    break;
  }
  case comments::CommandTraits::KCI_return:
  case comments::CommandTraits::KCI_returns:
    if (!ReturnParagraph) {
      ReturnParagraph = B->getParagraph();
      return;
    }
    break;
  case comments::CommandTraits::KCI_retval:
    // Only consider retval commands having an argument.
    // The argument contains the described return value which is needed to
    // convert it to markup.
    if (B->getNumArgs() == 1)
      RetvalCommands.push_back(B);
    return;
  default:
    break;
  }

  // For all other commands, we store them in the BlockCommands map.
  // This allows us to keep the order of the comments.
  BlockCommands[CommentPartIndex] = B;
  CommentPartIndex++;
}

void SymbolDocCommentVisitor::briefToMarkup(markup::Paragraph &Out) const {
  if (!BriefParagraph)
    return;
  ParagraphToMarkupDocument(Out, Traits).visit(BriefParagraph);
}

void SymbolDocCommentVisitor::returnToMarkup(markup::Paragraph &Out) const {
  if (!ReturnParagraph)
    return;
  ParagraphToMarkupDocument(Out, Traits).visit(ReturnParagraph);
}

void SymbolDocCommentVisitor::parameterDocToMarkup(
    StringRef ParamName, markup::Paragraph &Out) const {
  if (ParamName.empty())
    return;

  if (const auto *P = Parameters.lookup(ParamName)) {
    ParagraphToMarkupDocument(Out, Traits).visit(P->getParagraph());
  }
}

void SymbolDocCommentVisitor::parameterDocToString(
    StringRef ParamName, llvm::raw_string_ostream &Out) const {
  if (ParamName.empty())
    return;

  if (const auto *P = Parameters.lookup(ParamName)) {
    ParagraphToString(Out, Traits).visit(P->getParagraph());
  }
}

void SymbolDocCommentVisitor::detailedDocToMarkup(markup::Document &Out) const {
  for (unsigned I = 0; I < CommentPartIndex; ++I) {
    if (const auto *BC = BlockCommands.lookup(I)) {
      BlockCommentToMarkupDocument(Out, Traits).visit(BC);
    } else if (const auto *P = FreeParagraphs.lookup(I)) {
      ParagraphToMarkupDocument(Out.addParagraph(), Traits).visit(P);
    }
  }
}

void SymbolDocCommentVisitor::templateTypeParmDocToMarkup(
    StringRef TemplateParamName, markup::Paragraph &Out) const {
  if (TemplateParamName.empty())
    return;

  if (const auto *TP = TemplateParameters.lookup(TemplateParamName)) {
    ParagraphToMarkupDocument(Out, Traits).visit(TP->getParagraph());
  }
}

void SymbolDocCommentVisitor::templateTypeParmDocToString(
    StringRef TemplateParamName, llvm::raw_string_ostream &Out) const {
  if (TemplateParamName.empty())
    return;

  if (const auto *P = TemplateParameters.lookup(TemplateParamName)) {
    ParagraphToString(Out, Traits).visit(P->getParagraph());
  }
}

void SymbolDocCommentVisitor::retvalsToMarkup(markup::Document &Out) const {
  if (RetvalCommands.empty())
    return;
  markup::BulletList &BL = Out.addBulletList();
  for (const auto *P : RetvalCommands) {
    BlockCommentToMarkupDocument(BL.addItem(), Traits).visit(P);
  }
}

namespace {

void convertKernelDocInlineMarkup(llvm::StringRef Text,
                                  markup::Paragraph &Para) {
  unsigned I = 0;
  unsigned Start = 0;
  while (I < Text.size()) {
    char C = Text[I];

    // Double-backtick literal: ``text``
    if (C == '`' && I + 1 < Text.size() && Text[I + 1] == '`') {
      auto Close = Text.find("``", I + 2);
      if (Close != StringRef::npos) {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(I + 2, Close));
        I = Close + 2;
        Start = I;
        continue;
      }
    }

    // &struct name, &enum name, &typedef name, &struct->member
    if (C == '&') {
      unsigned J = I + 1;
      // Optional keyword: struct, enum, typedef, union
      unsigned KeywordEnd = J;
      while (KeywordEnd < Text.size() &&
             (llvm::isAlpha(Text[KeywordEnd]) || Text[KeywordEnd] == '_'))
        ++KeywordEnd;
      StringRef MaybeKeyword = Text.slice(J, KeywordEnd);
      bool HasKeyword = (MaybeKeyword == "struct" || MaybeKeyword == "enum" ||
                         MaybeKeyword == "typedef" || MaybeKeyword == "union");
      unsigned NameStart = HasKeyword ? KeywordEnd : J;
      if (HasKeyword && NameStart < Text.size() && Text[NameStart] == ' ')
        ++NameStart;
      unsigned NameEnd = NameStart;
      while (NameEnd < Text.size() &&
             (llvm::isAlnum(Text[NameEnd]) || Text[NameEnd] == '_'))
        ++NameEnd;
      // Allow ->member or .member suffix
      if (NameEnd < Text.size() &&
          (Text[NameEnd] == '.' ||
           (NameEnd + 1 < Text.size() && Text[NameEnd] == '-' &&
            Text[NameEnd + 1] == '>'))) {
        unsigned MemberStart =
            Text[NameEnd] == '.' ? NameEnd + 1 : NameEnd + 2;
        unsigned MemberEnd = MemberStart;
        while (MemberEnd < Text.size() &&
               (llvm::isAlnum(Text[MemberEnd]) || Text[MemberEnd] == '_'))
          ++MemberEnd;
        if (MemberEnd > MemberStart)
          NameEnd = MemberEnd;
      }
      if (NameEnd > NameStart) {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(J, NameEnd));
        I = NameEnd;
        Start = I;
        continue;
      }
    }

    // %CONSTANT or %-ERRNO
    if (C == '%') {
      unsigned J = I + 1;
      if (J < Text.size() && Text[J] == '-')
        ++J;
      while (J < Text.size() && (llvm::isAlnum(Text[J]) || Text[J] == '_'))
        ++J;
      if (J > I + 1) {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(I + 1, J));
        I = J;
        Start = J;
        continue;
      }
    }

    // @parameter
    if (C == '@') {
      unsigned J = I + 1;
      while (J < Text.size() && (llvm::isAlnum(Text[J]) || Text[J] == '_'))
        ++J;
      if (J > I + 1) {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(I + 1, J));
        I = J;
        Start = J;
        continue;
      }
    }

    // $ENVVAR
    if (C == '$') {
      unsigned J = I + 1;
      while (J < Text.size() && (llvm::isAlnum(Text[J]) || Text[J] == '_'))
        ++J;
      if (J > I + 1) {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(I, J));
        I = J;
        Start = J;
        continue;
      }
    }

    // Bare function references: identifier()
    if ((llvm::isAlpha(C) || C == '_') &&
        (I == 0 || !llvm::isAlnum(Text[I - 1]))) {
      unsigned J = I + 1;
      while (J < Text.size() && (llvm::isAlnum(Text[J]) || Text[J] == '_'))
        ++J;
      if (J + 1 < Text.size() && Text[J] == '(' && Text[J + 1] == ')') {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(I, J + 2));
        I = J + 2;
        Start = I;
        continue;
      }
    }

    ++I;
  }
  if (Start < Text.size())
    Para.appendText(Text.slice(Start, Text.size()));
}

} // namespace

KernelDocInfo parseKernelDoc(llvm::StringRef Doc) {
  KernelDocInfo Info;

  enum State {
    Brief,
    Params,
    Returns,
    Section,
    Body,
    FencedCodeBlock,
    IndentedCodeBlock
  } St = Brief;
  std::string CurrentCodeBlock;
  std::string CurrentCodeLang;
  std::string CodeFence;
  std::string CurrentParagraph;

  auto FlushParagraph = [&] {
    StringRef Trimmed = StringRef(CurrentParagraph).trim();
    if (!Trimmed.empty()) {
      // RST :: literal block marker: strip trailing ::
      // "word::" → "word:", "word ::" → "word", "::" → nothing
      if (Trimmed.ends_with("::")) {
        StringRef WithoutDC = Trimmed.drop_back(2);
        if (WithoutDC.ends_with(' '))
          WithoutDC = WithoutDC.rtrim();
        else if (!WithoutDC.empty())
          WithoutDC = Trimmed.drop_back(1);
        if (!WithoutDC.empty())
          Info.Description.push_back(
              {KernelDocDescriptionBlock::Paragraph, WithoutDC.str(), ""});
      } else {
        Info.Description.push_back(
            {KernelDocDescriptionBlock::Paragraph, Trimmed.str(), ""});
      }
    }
    CurrentParagraph.clear();
  };

  // Detect named section headers: a capitalized word followed by ':'
  // at the start of a line. Matches kernel-doc convention for Context:,
  // Note:, Warning:, Locking:, etc.
  auto IsSectionHeader = [](StringRef T) -> bool {
    if (T.empty() || !llvm::isUpper(T[0]))
      return false;
    auto ColonPos = T.find(':');
    if (ColonPos == StringRef::npos || ColonPos < 2)
      return false;
    // Reject RST literal block markers like "Example::"
    if (ColonPos + 1 < T.size() && T[ColonPos + 1] == ':')
      return false;
    StringRef Name = T.slice(0, ColonPos);
    return llvm::all_of(Name,
                        [](char C) { return llvm::isAlnum(C) || C == '_'; });
  };

  auto FlushIndentedCodeBlock = [&] {
    StringRef Code = StringRef(CurrentCodeBlock).rtrim('\n');
    if (!Code.empty()) {
      // Strip common leading indentation from all non-empty lines.
      size_t MinIndent = StringRef::npos;
      StringRef L, R = Code;
      while (!R.empty()) {
        std::tie(L, R) = R.split('\n');
        if (!L.empty())
          MinIndent = std::min(MinIndent, L.size() - L.ltrim().size());
      }
      std::string Stripped;
      R = Code;
      bool First = true;
      while (!R.empty()) {
        std::tie(L, R) = R.split('\n');
        if (!First)
          Stripped += '\n';
        First = false;
        if (L.size() >= MinIndent)
          Stripped += L.drop_front(MinIndent).str();
      }
      Info.Description.push_back(
          {KernelDocDescriptionBlock::Code, std::move(Stripped), ""});
    }
    CurrentCodeBlock.clear();
  };

  StringRef Line, Rest;
  for (std::tie(Line, Rest) = Doc.split('\n');
       !(Line.empty() && Rest.empty());
       std::tie(Line, Rest) = Rest.split('\n')) {

    StringRef Trimmed = Line.ltrim();

    if (St == FencedCodeBlock) {
      if (Trimmed.starts_with(CodeFence)) {
        StringRef Code = StringRef(CurrentCodeBlock).rtrim('\n');
        if (!Code.empty())
          Info.Description.push_back(
              {KernelDocDescriptionBlock::Code, Code.str(), CurrentCodeLang});
        St = Body;
        continue;
      }
      CurrentCodeBlock += Line.str() + "\n";
      continue;
    }

    // RST-style indented code block: indented text after a blank line
    if (St == IndentedCodeBlock) {
      if (!Trimmed.empty() && (Line[0] == ' ' || Line[0] == '\t')) {
        CurrentCodeBlock += Line.str() + "\n";
        continue;
      }
      if (Trimmed.empty()) {
        CurrentCodeBlock += "\n";
        continue;
      }
      // Non-indented, non-blank line ends the code block.
      FlushIndentedCodeBlock();
      St = Body;
      // Fall through to process this line normally.
    }

    // Markdown fenced code block: ```lang or ~~~
    if (Trimmed.starts_with("```") || Trimmed.starts_with("~~~")) {
      if (St == Body)
        FlushParagraph();
      CodeFence =
          Trimmed.take_while([](char C) { return C == '`' || C == '~'; }).str();
      CurrentCodeLang = Trimmed.drop_front(CodeFence.size()).ltrim().str();
      CurrentCodeBlock.clear();
      St = FencedCodeBlock;
      continue;
    }

    // Brief line: "function_name() - Brief description" or just first
    // non-empty line. May span multiple lines until a @param, blank line,
    // or a named section/tag is seen.
    if (St == Brief) {
      if (Trimmed.empty()) {
        if (!Info.Brief.empty())
          St = Params;
        continue;
      }
      // End brief on structured tags — fall through to their handlers.
      if (!Info.Brief.empty() &&
          (Trimmed.starts_with("@") || IsSectionHeader(Trimmed))) {
        St = Params;
      } else {
        if (Info.Brief.empty()) {
          auto DashPos = Trimmed.find(" - ");
          if (DashPos != StringRef::npos) {
            Info.Brief = Trimmed.drop_front(DashPos + 3).str();
          } else if (Trimmed.starts_with("@")) {
            // Inline member doc: /** @member: description */
            auto ColonPos = Trimmed.find(':');
            if (ColonPos != StringRef::npos)
              Info.Brief = Trimmed.drop_front(ColonPos + 1).ltrim().str();
            else
              Info.Brief = Trimmed.str();
          } else {
            // Try "identifier():" or "identifier:" colon-style brief.
            bool FoundColonBrief = false;
            unsigned J = 0;
            while (J < Trimmed.size() &&
                   (llvm::isAlnum(Trimmed[J]) || Trimmed[J] == '_'))
              ++J;
            if (J > 0 && J < Trimmed.size()) {
              unsigned K = J;
              if (K + 1 < Trimmed.size() && Trimmed[K] == '(' &&
                  Trimmed[K + 1] == ')')
                K += 2;
              if (K < Trimmed.size() && Trimmed[K] == ' ')
                ++K;
              if (K < Trimmed.size() && Trimmed[K] == ':' &&
                  (K + 1 >= Trimmed.size() || Trimmed[K + 1] != ':')) {
                Info.Brief = Trimmed.drop_front(K + 1).ltrim().str();
                FoundColonBrief = true;
              }
            }
            if (!FoundColonBrief)
              Info.Brief = Trimmed.str();
          }
        } else {
          Info.Brief += " " + Trimmed.str();
        }
        continue;
      }
    }

    // @return: / @returns: — treated as Return section per reference parser
    if (Trimmed.starts_with_insensitive("@return:") ||
        Trimmed.starts_with_insensitive("@returns:")) {
      if (St == Body)
        FlushParagraph();
      St = Returns;
      StringRef Tag = Trimmed.starts_with_insensitive("@returns:")
                          ? Trimmed.take_front(9)
                          : Trimmed.take_front(8);
      Info.Returns = Trimmed.drop_front(Tag.size()).ltrim().str();
      continue;
    }

    // @...: for variadic arguments
    if (Trimmed.starts_with("@...:")) {
      if (St == Body)
        FlushParagraph();
      St = Params;
      StringRef Desc = Trimmed.drop_front(5).ltrim();
      Info.Params.push_back({"...", Desc.str()});
      continue;
    }

    // Parameter line: @name: description
    if (Trimmed.starts_with("@")) {
      auto ColonPos = Trimmed.find(':');
      if (ColonPos != StringRef::npos && ColonPos > 1) {
        StringRef ParamName = Trimmed.slice(1, ColonPos);
        bool IsParam = true;
        for (unsigned K = 0; K < ParamName.size(); ++K) {
          char C = ParamName[K];
          if (llvm::isAlnum(C) || C == '_' || C == '.')
            continue;
          if (C == '-' && K + 1 < ParamName.size() && ParamName[K + 1] == '>') {
            ++K; // skip '>'
            continue;
          }
          IsParam = false;
          break;
        }
        if (IsParam) {
          if (St == Body)
            FlushParagraph();
          St = Params;
          StringRef Desc = Trimmed.drop_front(ColonPos + 1).ltrim();
          Info.Params.push_back({ParamName.str(), Desc.str()});
          continue;
        }
      }
    }

    // Return: or Returns: description (but not Return:: literal block marker)
    if ((Trimmed.starts_with_insensitive("Return:") &&
         !Trimmed.starts_with_insensitive("Return::")) ||
        (Trimmed.starts_with_insensitive("Returns:") &&
         !Trimmed.starts_with_insensitive("Returns::"))) {
      if (St == Body)
        FlushParagraph();
      St = Returns;
      StringRef Tag = Trimmed.starts_with_insensitive("Returns:")
                          ? Trimmed.take_front(8)
                          : Trimmed.take_front(7);
      Info.Returns = Trimmed.drop_front(Tag.size()).ltrim().str();
      continue;
    }

    // Description: is an optional explicit section header — strip the tag
    // and treat the remainder as the start of body text.
    if (Trimmed.starts_with_insensitive("Description:") &&
        !Trimmed.starts_with_insensitive("Description::")) {
      if (St == Body)
        FlushParagraph();
      St = Body;
      StringRef Desc = Trimmed.drop_front(12).ltrim();
      if (!Desc.empty()) {
        CurrentParagraph = Desc.str();
      }
      continue;
    }

    // Generic named section: "Word:" at start of line.
    // Handles Context:, Note:, Warning:, Locking:, etc.
    // When in a continuation state, only match non-indented lines as
    // section headers — indented lines are continuation text.
    bool IsIndented = !Line.empty() && (Line[0] == ' ' || Line[0] == '\t');
    bool InContinuation = (St == Params || St == Returns || St == Section);
    if (IsSectionHeader(Trimmed) && !(InContinuation && IsIndented)) {
      if (St == Body)
        FlushParagraph();
      St = Section;
      auto ColonPos = Trimmed.find(':');
      StringRef Name = Trimmed.slice(0, ColonPos);
      StringRef Desc = Trimmed.drop_front(ColonPos + 1).ltrim();
      Info.Sections.push_back({Name.str(), Desc.str()});
      continue;
    }

    // Param continuation: indented or non-tag non-empty line while in Params
    if (St == Params && !Trimmed.empty() && !Info.Params.empty()) {
      Info.Params.back().Description += " " + Trimmed.str();
      continue;
    }

    // Returns continuation: detect RST list items (* or -)
    if (St == Returns && !Trimmed.empty()) {
      if (Trimmed.starts_with("* ") || Trimmed.starts_with("- ")) {
        Info.ReturnItems.push_back(Trimmed.drop_front(2).str());
      } else if (!Info.ReturnItems.empty()) {
        Info.ReturnItems.back() += " " + Trimmed.str();
      } else {
        Info.Returns += " " + Trimmed.str();
      }
      continue;
    }

    // Section continuation
    if (St == Section && !Trimmed.empty() && !Info.Sections.empty()) {
      Info.Sections.back().Description += " " + Trimmed.str();
      continue;
    }

    // Transition to body on blank line or first non-structured content
    if (St == Params || St == Returns || St == Section) {
      St = Body;
    }

    // Body text
    if (Trimmed.empty()) {
      FlushParagraph();
    } else if (CurrentParagraph.empty() && Line[0] == '\t') {
      CurrentCodeBlock = Line.str() + "\n";
      St = IndentedCodeBlock;
    } else if (CurrentParagraph.empty() && Line.size() >= 2 &&
               Line[0] == ' ' && Line[1] == ' ') {
      CurrentCodeBlock = Line.str() + "\n";
      St = IndentedCodeBlock;
    } else {
      if (!CurrentParagraph.empty())
        CurrentParagraph += " ";
      CurrentParagraph += Trimmed.str();
    }
  }

  if (St == IndentedCodeBlock)
    FlushIndentedCodeBlock();
  FlushParagraph();

  return Info;
}

void renderKernelDocToMarkup(const KernelDocInfo &Info,
                             markup::Document &Output) {
  if (!Info.Brief.empty())
    convertKernelDocInlineMarkup(Info.Brief, Output.addParagraph());

  for (const auto &Block : Info.Description) {
    if (Block.BlockKind == KernelDocDescriptionBlock::Paragraph)
      convertKernelDocInlineMarkup(Block.Text, Output.addParagraph());
    else
      Output.addCodeBlock(Block.Text, Block.Language);
  }

  if (!Info.Params.empty()) {
    Output.addHeading(3).appendText("Parameters");
    markup::BulletList &L = Output.addBulletList();
    for (const auto &P : Info.Params) {
      markup::Paragraph &Para = L.addItem().addParagraph();
      Para.appendCode(P.Name);
      if (!P.Description.empty()) {
        Para.appendText(" - ");
        convertKernelDocInlineMarkup(P.Description, Para);
      }
    }
  }

  if (!Info.Returns.empty() || !Info.ReturnItems.empty()) {
    Output.addHeading(3).appendText("Returns");
    if (!Info.Returns.empty())
      convertKernelDocInlineMarkup(Info.Returns, Output.addParagraph());
    if (!Info.ReturnItems.empty()) {
      markup::BulletList &L = Output.addBulletList();
      for (const auto &Item : Info.ReturnItems) {
        markup::Paragraph &Para = L.addItem().addParagraph();
        convertKernelDocInlineMarkup(Item, Para);
      }
    }
  }

  for (const auto &S : Info.Sections) {
    Output.addHeading(3).appendText(S.Name);
    convertKernelDocInlineMarkup(S.Description, Output.addParagraph());
  }
}

namespace {

std::pair<std::string, StringRef> splitAnnotations(StringRef Text) {
  StringRef T = Text.ltrim();
  std::string Annotations;
  while (T.starts_with("(")) {
    auto Close = T.find(')');
    if (Close == StringRef::npos)
      break;
    if (!Annotations.empty())
      Annotations += " ";
    Annotations += T.slice(0, Close + 1).str();
    T = T.drop_front(Close + 1).ltrim();
  }
  if (Annotations.empty())
    return {"", Text};
  T.consume_front(":");
  T = T.ltrim();
  return {Annotations, T};
}

void convertGTKDocInlineMarkup(llvm::StringRef Text,
                               markup::Paragraph &Para) {
  unsigned I = 0;
  unsigned Start = 0;
  while (I < Text.size()) {
    char C = Text[I];

    // gi-docgen cross-references: [type@Namespace.Name]
    if (C == '[') {
      auto Close = Text.find(']', I + 1);
      if (Close != StringRef::npos) {
        StringRef Inner = Text.slice(I + 1, Close);
        auto AtPos = Inner.find('@');
        if (AtPos != StringRef::npos) {
          StringRef LinkTarget = Inner.drop_front(AtPos + 1);
          // Extract the name after the last dot: Namespace.Name -> Name
          auto DotPos = LinkTarget.rfind('.');
          StringRef DisplayName =
              DotPos != StringRef::npos
                  ? LinkTarget.drop_front(DotPos + 1)
                  : LinkTarget;
          if (!DisplayName.empty()) {
            if (I > Start)
              Para.appendText(Text.slice(Start, I));
            Para.appendCode(DisplayName);
            I = Close + 1;
            Start = I;
            continue;
          }
        }
      }
    }

    if (C == '#' || C == '%' || C == '@') {
      if (I > Start)
        Para.appendText(Text.slice(Start, I));
      unsigned J = I + 1;
      while (J < Text.size() && (llvm::isAlnum(Text[J]) || Text[J] == '_'))
        ++J;
      if (J > I + 1) {
        Para.appendCode(Text.slice(I + 1, J));
        I = J;
        Start = J;
        continue;
      }
    }

    // Bare function references: identifier()
    if ((llvm::isAlpha(C) || C == '_') && (I == 0 || !llvm::isAlnum(Text[I - 1]))) {
      unsigned J = I + 1;
      while (J < Text.size() && (llvm::isAlnum(Text[J]) || Text[J] == '_'))
        ++J;
      if (J + 1 < Text.size() && Text[J] == '(' && Text[J + 1] == ')') {
        if (I > Start)
          Para.appendText(Text.slice(Start, I));
        Para.appendCode(Text.slice(I, J + 2));
        I = J + 2;
        Start = I;
        continue;
      }
    }

    ++I;
  }
  if (Start < Text.size())
    Para.appendText(Text.slice(Start, Text.size()));
}

} // namespace

GTKDocInfo parseGTKDoc(llvm::StringRef Doc) {
  GTKDocInfo Info;

  enum State {
    Header,
    Params,
    Returns,
    Body,
    CodeBlock,
    FencedCodeBlock,
    Table
  } St = Header;
  std::string CurrentCodeBlock;
  std::string CurrentCodeLang;
  std::string CodeFence;
  std::string CurrentParagraph;
  std::string CurrentTable;

  auto FlushParagraph = [&] {
    StringRef Trimmed = StringRef(CurrentParagraph).trim();
    if (!Trimmed.empty())
      Info.Description.push_back(
          {GTKDocDescriptionBlock::Paragraph, Trimmed.str(), ""});
    CurrentParagraph.clear();
  };

  auto NormalizeTableRow = [](StringRef Row) -> std::string {
    StringRef T = Row.trim();
    std::string Result;
    if (!T.starts_with("|"))
      Result += "| ";
    Result += T.str();
    if (!T.ends_with("|"))
      Result += " |";
    return Result;
  };

  auto FlushTable = [&] {
    StringRef Trimmed = StringRef(CurrentTable).rtrim('\n');
    if (Trimmed.empty()) {
      CurrentTable.clear();
      return;
    }
    std::string GFMTable;
    llvm::SmallVector<StringRef> Rows;
    Trimmed.split(Rows, '\n');
    for (auto Row : Rows) {
      if (!GFMTable.empty())
        GFMTable += '\n';
      GFMTable += NormalizeTableRow(Row);
    }
    Info.Description.push_back(
        {GTKDocDescriptionBlock::Table, std::move(GFMTable), ""});
    CurrentTable.clear();
  };

  auto IsTableSeparator = [](StringRef T) {
    return T.find_first_not_of("-|: ") == StringRef::npos && T.contains('-') &&
           T.contains('|');
  };

  auto IsTableLine = [&](StringRef S, bool InTable) {
    StringRef T = S.ltrim();
    if (T.empty())
      return false;
    if (IsTableSeparator(T))
      return true;
    if (T.starts_with("|") || T.ends_with("|"))
      return true;
    // Inside a table, any line with interior | is a continuation row.
    if (InTable && T.contains('|'))
      return true;
    return false;
  };

  StringRef Line, Rest;
  bool SeenNonEmpty = false;
  for (std::tie(Line, Rest) = Doc.split('\n');
       !(Line.empty() && Rest.empty());
       std::tie(Line, Rest) = Rest.split('\n')) {

    StringRef Trimmed = Line.ltrim();

    // Skip leading blank lines and the function name header line.
    if (!SeenNonEmpty) {
      if (Trimmed.empty())
        continue;
      SeenNonEmpty = true;
      if (Trimmed.ends_with(":") &&
          Trimmed.drop_back(1).trim().find(' ') == StringRef::npos)
        continue;
    }

    if (St == CodeBlock) {
      if (Trimmed == "]|") {
        StringRef Code = StringRef(CurrentCodeBlock).rtrim('\n');
        if (!Code.empty())
          Info.Description.push_back(
              {GTKDocDescriptionBlock::Code, Code.str(), CurrentCodeLang});
        St = Body;
        continue;
      }
      CurrentCodeBlock += Line.str() + "\n";
      continue;
    }

    if (St == FencedCodeBlock) {
      if (Trimmed.starts_with(CodeFence)) {
        StringRef Code = StringRef(CurrentCodeBlock).rtrim('\n');
        if (!Code.empty())
          Info.Description.push_back(
              {GTKDocDescriptionBlock::Code, Code.str(), CurrentCodeLang});
        St = Body;
        continue;
      }
      CurrentCodeBlock += Line.str() + "\n";
      continue;
    }

    // Code block start: |[<!-- language="C" -->
    if (Trimmed.starts_with("|[")) {
      if (St == Body)
        FlushParagraph();
      St = CodeBlock;
      CurrentCodeBlock.clear();
      CurrentCodeLang = "c";
      StringRef After = Trimmed.drop_front(2).ltrim();
      if (After.consume_front("<!--")) {
        auto LangPos = After.find("language=\"");
        if (LangPos != StringRef::npos) {
          StringRef LangStr = After.drop_front(LangPos + 10);
          auto End = LangStr.find('"');
          if (End != StringRef::npos)
            CurrentCodeLang = LangStr.take_front(End).str();
        }
      }
      continue;
    }

    // Markdown fenced code block: ```lang, ``` { .lang }, or ~~~
    if (Trimmed.starts_with("```") || Trimmed.starts_with("~~~")) {
      if (St == Body)
        FlushParagraph();
      CodeFence =
          Trimmed.take_while([](char C) { return C == '`' || C == '~'; }).str();
      StringRef LangSpec = Trimmed.drop_front(CodeFence.size()).ltrim();
      // gi-docgen/Pandoc attribute syntax: ``` { .lang }
      if (LangSpec.starts_with("{")) {
        StringRef Inner = LangSpec.drop_front(1).ltrim();
        if (Inner.consume_front(".")) {
          auto End = Inner.find_first_of(" }");
          CurrentCodeLang =
              Inner.take_front(End == StringRef::npos ? Inner.size() : End)
                  .str();
        } else {
          CurrentCodeLang = "c";
        }
      } else {
        CurrentCodeLang = LangSpec.str();
      }
      CurrentCodeBlock.clear();
      St = FencedCodeBlock;
      continue;
    }

    // Parameter line: @name: description  or  @name: (annotations): description
    if (Trimmed.starts_with("@")) {
      auto ColonPos = Trimmed.find(':');
      if (ColonPos != StringRef::npos && ColonPos > 1) {
        StringRef ParamName = Trimmed.slice(1, ColonPos);
        bool IsParam = true;
        for (char C : ParamName)
          if (!(llvm::isAlnum(C) || C == '_')) {
            IsParam = false;
            break;
          }
        if (IsParam) {
          St = Params;
          StringRef Desc = Trimmed.drop_front(ColonPos + 1).ltrim();
          Info.Params.push_back({ParamName.str(), Desc.str()});
          continue;
        }
      }
    }

    // Returns: description
    if (Trimmed.starts_with("Returns:")) {
      St = Returns;
      Info.Returns = Trimmed.drop_front(8).ltrim().str();
      continue;
    }

    // Since: version
    if (Trimmed.starts_with("Since:")) {
      Info.Since = Trimmed.drop_front(6).ltrim().str();
      continue;
    }

    // Deprecated: version. description
    if (Trimmed.starts_with("Deprecated:")) {
      Info.Deprecated = Trimmed.drop_front(11).ltrim().str();
      continue;
    }

    // Stability: Stable|Unstable|Private
    if (Trimmed.starts_with("Stability:")) {
      Info.Stability = Trimmed.drop_front(10).ltrim().str();
      continue;
    }

    // Param continuation: any non-empty line that isn't a new tag.
    if (St == Params && !Trimmed.empty()) {
      Info.Params.back().Description += " " + Trimmed.str();
      continue;
    }

    // Returns continuation: any non-empty line that isn't a new tag.
    if (St == Returns && !Trimmed.empty()) {
      Info.Returns += " " + Trimmed.str();
      continue;
    }

    // Table continuation.
    if (St == Table) {
      if (IsTableLine(Line, true)) {
        CurrentTable += Line.str() + "\n";
        continue;
      }
      FlushTable();
      St = Body;
      // Fall through to body handling.
    }

    // Markdown table: detect separator row and pull back header from paragraph.
    if (IsTableSeparator(Trimmed)) {
      if (!CurrentParagraph.empty() && CurrentParagraph.find('|') != std::string::npos) {
        CurrentTable = CurrentParagraph + "\n" + Line.str() + "\n";
        CurrentParagraph.clear();
      } else {
        if (St == Body)
          FlushParagraph();
        CurrentTable = Line.str() + "\n";
      }
      St = Table;
      continue;
    }

    // Markdown table start (line with leading/trailing |).
    if (IsTableLine(Line, false)) {
      if (St == Body)
        FlushParagraph();
      CurrentTable = Line.str() + "\n";
      St = Table;
      continue;
    }

    // Transition to body on first non-structured content.
    if (St == Header || St == Params || St == Returns) {
      St = Body;
    }

    // Body text.
    if (Trimmed.empty()) {
      FlushParagraph();
    } else {
      if (!CurrentParagraph.empty())
        CurrentParagraph += " ";
      CurrentParagraph += Trimmed.str();
    }
  }

  FlushTable();
  FlushParagraph();

  return Info;
}

void renderGTKDocToMarkup(const GTKDocInfo &Info, markup::Document &Output) {
  if (!Info.Description.empty()) {
    for (const auto &Block : Info.Description) {
      if (Block.BlockKind == GTKDocDescriptionBlock::Paragraph)
        convertGTKDocInlineMarkup(Block.Text, Output.addParagraph());
      else if (Block.BlockKind == GTKDocDescriptionBlock::Table)
        Output.addRawMarkdown(Block.Text);
      else
        Output.addCodeBlock(Block.Text, Block.Language);
    }
  }

  if (!Info.Params.empty()) {
    Output.addHeading(3).appendText("Parameters");
    markup::BulletList &L = Output.addBulletList();
    for (const auto &P : Info.Params) {
      markup::Paragraph &Para = L.addItem().addParagraph();
      Para.appendCode(P.Name);
      if (!P.Description.empty()) {
        auto [Ann, Desc] = splitAnnotations(P.Description);
        Para.appendText(" - ");
        convertGTKDocInlineMarkup(Desc, Para);
        if (!Ann.empty()) {
          Para.appendText(" ");
          Para.appendEmphasizedText(Ann);
        }
      }
    }
  }

  if (!Info.Returns.empty()) {
    Output.addHeading(3).appendText("Returns");
    auto &RetPara = Output.addParagraph();
    auto [Ann, Desc] = splitAnnotations(Info.Returns);
    convertGTKDocInlineMarkup(Desc, RetPara);
    if (!Ann.empty()) {
      RetPara.appendText(" ");
      RetPara.appendEmphasizedText(Ann);
    }
  }

  if (!Info.Deprecated.empty()) {
    Output.addParagraph().appendBoldText("Deprecated:").appendText(
        " " + Info.Deprecated);
  }

  if (!Info.Since.empty()) {
    Output.addParagraph().appendBoldText("Since:").appendText(
        " " + Info.Since);
  }

  if (!Info.Stability.empty()) {
    Output.addParagraph().appendBoldText("Stability:").appendText(
        " " + Info.Stability);
  }
}

} // namespace clangd
} // namespace clang
