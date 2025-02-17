//===--- TestTU.cpp - Scratch source files for testing --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TestTU.h"
#include "TestFS.h"
#include "index/FileIndex.h"
#include "index/MemIndex.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/Utils.h"

namespace clang {
namespace clangd {

ParsedAST TestTU::build() const {
  std::string FullFilename = testPath(Filename),
              FullHeaderName = testPath(HeaderFilename),
              ImportThunk = testPath("import_thunk.h");
  // We want to implicitly include HeaderFilename without messing up offsets.
  // -include achieves this, but sometimes we want #import (to simulate a header
  // guard without messing up offsets). In this case, use an intermediate file.
  std::string ThunkContents = "#import \"" + FullHeaderName + "\"\n";

  llvm::StringMap<std::string> Files(AdditionalFiles);
  Files[FullFilename] = Code;
  Files[FullHeaderName] = HeaderCode;
  Files[ImportThunk] = ThunkContents;

  std::vector<const char *> Cmd = {"clang"};
  // FIXME: this shouldn't need to be conditional, but it breaks a
  // GoToDefinition test for some reason (getMacroArgExpandedLocation fails).
  if (!HeaderCode.empty()) {
    Cmd.push_back("-include");
    Cmd.push_back(ImplicitHeaderGuard ? ImportThunk.c_str()
                                      : FullHeaderName.c_str());
    // ms-compatibility changes the meaning of #import.
    // The default is OS-dependent (on on windows), ensure it's off.
    if (ImplicitHeaderGuard)
      Cmd.push_back("-fno-ms-compatibility");
  }
  Cmd.insert(Cmd.end(), ExtraArgs.begin(), ExtraArgs.end());
  // Put the file name at the end -- this allows the extra arg (-xc++) to
  // override the language setting.
  Cmd.push_back(FullFilename.c_str());
  ParseInputs Inputs;
  Inputs.CompileCommand.Filename = FullFilename;
  Inputs.CompileCommand.CommandLine = {Cmd.begin(), Cmd.end()};
  Inputs.CompileCommand.Directory = testRoot();
  Inputs.Contents = Code;
  Inputs.FS = buildTestFS(Files);
  Inputs.Opts = ParseOptions();
  Inputs.Opts.ClangTidyOpts.Checks = ClangTidyChecks;
  Inputs.Opts.ClangTidyOpts.WarningsAsErrors = ClangTidyWarningsAsErrors;
  Inputs.Index = ExternalIndex;
  if (Inputs.Index)
    Inputs.Opts.SuggestMissingIncludes = true;
  auto CI = buildCompilerInvocation(Inputs);
  assert(CI && "Failed to build compilation invocation.");
  auto Preamble =
      buildPreamble(FullFilename, *CI,
                    /*OldPreamble=*/nullptr,
                    /*OldCompileCommand=*/Inputs.CompileCommand, Inputs,
                    /*StoreInMemory=*/true, /*PreambleCallback=*/nullptr);
  auto AST = buildAST(FullFilename, std::move(CI), Inputs, Preamble);
  if (!AST.hasValue()) {
    ADD_FAILURE() << "Failed to build code:\n" << Code;
    llvm_unreachable("Failed to build TestTU!");
  }
  return std::move(*AST);
}

SymbolSlab TestTU::headerSymbols() const {
  auto AST = build();
  return std::get<0>(indexHeaderSymbols(AST.getASTContext(),
                                        AST.getPreprocessorPtr(),
                                        AST.getCanonicalIncludes()));
}

std::unique_ptr<SymbolIndex> TestTU::index() const {
  auto AST = build();
  auto Idx = std::make_unique<FileIndex>(/*UseDex=*/true);
  Idx->updatePreamble(Filename, AST.getASTContext(), AST.getPreprocessorPtr(),
                      AST.getCanonicalIncludes());
  Idx->updateMain(Filename, AST);
  return std::move(Idx);
}

const Symbol &findSymbol(const SymbolSlab &Slab, llvm::StringRef QName) {
  const Symbol *Result = nullptr;
  for (const Symbol &S : Slab) {
    if (QName != (S.Scope + S.Name).str())
      continue;
    if (Result) {
      ADD_FAILURE() << "Multiple symbols named " << QName << ":\n"
                    << *Result << "\n---\n"
                    << S;
      assert(false && "QName is not unique");
    }
    Result = &S;
  }
  if (!Result) {
    ADD_FAILURE() << "No symbol named " << QName << " in "
                  << ::testing::PrintToString(Slab);
    assert(false && "No symbol with QName");
  }
  return *Result;
}

const NamedDecl &findDecl(ParsedAST &AST, llvm::StringRef QName) {
  llvm::SmallVector<llvm::StringRef, 4> Components;
  QName.split(Components, "::");

  auto &Ctx = AST.getASTContext();
  auto LookupDecl = [&Ctx](const DeclContext &Scope,
                           llvm::StringRef Name) -> const NamedDecl & {
    auto LookupRes = Scope.lookup(DeclarationName(&Ctx.Idents.get(Name)));
    assert(!LookupRes.empty() && "Lookup failed");
    assert(LookupRes.size() == 1 && "Lookup returned multiple results");
    return *LookupRes.front();
  };

  const DeclContext *Scope = Ctx.getTranslationUnitDecl();
  for (auto NameIt = Components.begin(), End = Components.end() - 1;
       NameIt != End; ++NameIt) {
    Scope = &cast<DeclContext>(LookupDecl(*Scope, *NameIt));
  }
  return LookupDecl(*Scope, Components.back());
}

const NamedDecl &findDecl(ParsedAST &AST,
                          std::function<bool(const NamedDecl &)> Filter) {
  struct Visitor : RecursiveASTVisitor<Visitor> {
    decltype(Filter) F;
    llvm::SmallVector<const NamedDecl *, 1> Decls;
    bool VisitNamedDecl(const NamedDecl *ND) {
      if (F(*ND))
        Decls.push_back(ND);
      return true;
    }
  } Visitor;
  Visitor.F = Filter;
  Visitor.TraverseDecl(AST.getASTContext().getTranslationUnitDecl());
  if (Visitor.Decls.size() != 1) {
    ADD_FAILURE() << Visitor.Decls.size() << " symbols matched.";
    assert(Visitor.Decls.size() == 1);
  }
  return *Visitor.Decls.front();
}

const NamedDecl &findUnqualifiedDecl(ParsedAST &AST, llvm::StringRef Name) {
  return findDecl(AST, [Name](const NamedDecl &ND) {
    if (auto *ID = ND.getIdentifier())
      if (ID->getName() == Name)
        return true;
    return false;
  });
}

} // namespace clangd
} // namespace clang
