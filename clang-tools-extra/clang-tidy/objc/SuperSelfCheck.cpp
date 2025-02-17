//===--- SuperSelfCheck.cpp - clang-tidy ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SuperSelfCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace objc {

namespace {

/// \brief Matches Objective-C methods in the initializer family.
///
/// Example matches -init and -initWithInt:.
///   (matcher = objcMethodDecl(isInitializer()))
/// \code
///   @interface Foo
///   - (instancetype)init;
///   - (instancetype)initWithInt:(int)i;
///   + (instancetype)init;
///   - (void)bar;
///   @end
/// \endcode
AST_MATCHER(ObjCMethodDecl, isInitializer) {
  return Node.getMethodFamily() == OMF_init;
}

/// \brief Matches Objective-C implementations with interfaces that match
/// \c Base.
///
/// Example matches implementation declarations for X.
///   (matcher = objcImplementationDecl(hasInterface(hasName("X"))))
/// \code
///   @interface X
///   @end
///   @implementation X
///   @end
///   @interface Y
//    @end
///   @implementation Y
///   @end
/// \endcode
AST_MATCHER_P(ObjCImplementationDecl, hasInterface,
              ast_matchers::internal::Matcher<ObjCInterfaceDecl>, Base) {
  const ObjCInterfaceDecl *InterfaceDecl = Node.getClassInterface();
  return Base.matches(*InterfaceDecl, Finder, Builder);
}

/// \brief Matches Objective-C message expressions where the receiver is the
/// super instance.
///
/// Example matches the invocations of -banana and -orange.
///   (matcher = objcMessageExpr(isMessagingSuperInstance()))
/// \code
///   - (void)banana {
///     [self apple]
///     [super banana];
///     [super orange];
///   }
/// \endcode
AST_MATCHER(ObjCMessageExpr, isMessagingSuperInstance) {
  return Node.getReceiverKind() == ObjCMessageExpr::SuperInstance;
}

} // namespace

void SuperSelfCheck::registerMatchers(MatchFinder *Finder) {
  // This check should only be applied to Objective-C sources.
  if (!getLangOpts().ObjC)
    return;

  Finder->addMatcher(
      objcMessageExpr(hasSelector("self"), isMessagingSuperInstance(),
                      hasAncestor(objcMethodDecl(
                          isInitializer(),
                          hasDeclContext(objcImplementationDecl(hasInterface(
                              isDerivedFrom(hasName("NSObject"))))))))
          .bind("message"),
      this);
}

void SuperSelfCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Message = Result.Nodes.getNodeAs<ObjCMessageExpr>("message");

  auto Diag = diag(Message->getExprLoc(), "suspicious invocation of %0 in "
                                          "initializer; did you mean to "
                                          "invoke a superclass initializer?")
              << Message->getMethodDecl();

  SourceLocation ReceiverLoc = Message->getReceiverRange().getBegin();
  if (ReceiverLoc.isMacroID() || ReceiverLoc.isInvalid())
    return;

  SourceLocation SelectorLoc = Message->getSelectorStartLoc();
  if (SelectorLoc.isMacroID() || SelectorLoc.isInvalid())
    return;

  Diag << FixItHint::CreateReplacement(Message->getSourceRange(),
                                       StringRef("[super init]"));
}

} // namespace objc
} // namespace tidy
} // namespace clang
