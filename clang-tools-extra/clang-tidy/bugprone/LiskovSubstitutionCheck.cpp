//===--- LiskovSubstitutionCheck.cpp - clang-tidy -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LiskovSubstitutionCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void LiskovSubstitutionCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      cxxMethodDecl(isOverride(), has(compoundStmt())).bind("func"), this);
}

static bool findParentCall(const CXXMethodDecl *MatchedDecl, const Stmt *Node,
                           bool &InConditional) {
  for (auto const &Child : Node->children()) {
    if (!Child)
      continue;
    if (Child->getStmtClass() == Stmt::IfStmtClass ||
        Child->getStmtClass() == Stmt::SwitchStmtClass) {
      if (findParentCall(MatchedDecl, Child, InConditional)) {
        InConditional = true;
        return true;
      }
    } else if (Child->getStmtClass() == Stmt::CXXMemberCallExprClass) {
      auto const *Call = static_cast<const CXXMemberCallExpr *>(Child);
      // check if call references same function in parent
      auto const *Parent = Call->getMethodDecl();
      const auto *Corresponding =
          MatchedDecl->getCorrespondingMethodDeclaredInClass(
              Parent->getParent(), true);
      if (Parent->getName() == MatchedDecl->getName() &&
          Corresponding == Parent &&
          MatchedDecl->getParent()->isDerivedFrom(Parent->getParent())) {
        return true;
      }
    } else {
      if (findParentCall(MatchedDecl, Child, InConditional)) {
        return true;
      }
    }
  }

  return false;
}

void LiskovSubstitutionCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<CXXMethodDecl>("func");

  if (!MatchedDecl->getIdentifier())
    return;

  bool Conditional = false;
  if (findParentCall(MatchedDecl, MatchedDecl->getBody(), Conditional)) {
    if (Conditional) {
      diag(MatchedDecl->getLocation(),
           "virtual override function %0 is not calling parent implementation "
           "unconditionally.")
          << MatchedDecl;
    } else {
      return;
    }
  }

  diag(MatchedDecl->getLocation(),
       "virtual override function %0 is not calling parent implementation.")
      << MatchedDecl;
}

} // namespace clang::tidy::bugprone
