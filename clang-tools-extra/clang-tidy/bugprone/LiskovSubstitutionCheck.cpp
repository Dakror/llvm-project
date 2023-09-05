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

enum class Result {
  NotFound = 0,
  ParentFound,
  DependentParentFound,
  GrandparentFound
};

static Result findParentCall(const CXXMethodDecl *MatchedDecl,
                             const Stmt *Node) {
  for (auto const &Child : Node->children()) {
    if (!Child)
      continue;
    if (Child->getStmtClass() == Stmt::IfStmtClass ||
        Child->getStmtClass() == Stmt::SwitchStmtClass) {
      auto Res = findParentCall(MatchedDecl, Child);
      if (Res != Result::NotFound) {
        return Result::DependentParentFound;
      }
    } else if (Child->getStmtClass() == Stmt::CXXMemberCallExprClass) {
      auto const *Call = static_cast<const CXXMemberCallExpr *>(Child);
      // check if call references same function in parent
      auto const *Parent = Call->getMethodDecl();
      const auto *Corresponding =
          MatchedDecl->getCorrespondingMethodDeclaredInClass(
              Parent->getParent(), true);
      CXXBasePaths Paths;
      if (Parent->getName() == MatchedDecl->getName() &&
          Corresponding == Parent &&
          MatchedDecl->getParent()->isDerivedFrom(Parent->getParent(), Paths)) {
        // check call path to see if some parent was skipped
        for (auto const &Path : Paths) {
          for (auto const &Elem : Path) {
            auto const *Candidate =
                MatchedDecl->getCorrespondingMethodDeclaredInClass(
                    Elem.Base->getType()->getAsCXXRecordDecl(), true);
            if (Candidate && Candidate != Corresponding) {
              return Result::GrandparentFound;
            }
          }
        }
        return Result::ParentFound;
      }
    } else {
      auto Res = findParentCall(MatchedDecl, Child);
      if (Res != Result::NotFound) {
        return Res;
      }
    }
  }

  return Result::NotFound;
}

void LiskovSubstitutionCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<CXXMethodDecl>("func");

  if (!MatchedDecl->getIdentifier())
    return;

  // check if any parent class has an implementation for the given function
  CXXBasePaths Paths;
  if (!MatchedDecl->getParent()->lookupInBases(
          [&MatchedDecl](const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
            const auto *Method =
                MatchedDecl->getCorrespondingMethodDeclaredInClass(
                    Specifier->getType()->getAsCXXRecordDecl(), true);

            return Method && Method->hasBody();
          },
          Paths)) {
    return;
  }

  auto Res = findParentCall(MatchedDecl, MatchedDecl->getBody());
  switch (Res) {
  case Result::NotFound: {
    diag(MatchedDecl->getLocation(),
         "virtual override function %0 is not calling parent implementation.")
        << MatchedDecl;
    break;
  }
  case Result::DependentParentFound: {
    diag(MatchedDecl->getLocation(),
         "virtual override function %0 is not calling parent implementation "
         "unconditionally.")
        << MatchedDecl;
    break;
  }
  case Result::GrandparentFound: {
    diag(MatchedDecl->getLocation(), "virtual override function %0 is not "
                                     "calling direct parent implementation")
        << MatchedDecl;
    break;
  }
  default:
    break;
  }
}

} // namespace clang::tidy::bugprone
