//===- RecordName.h ------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_RECORDNAME_H
#define LLVM_DEBUGINFO_CODEVIEW_RECORDNAME_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
namespace codeview {
class TypeCollection;
class TypeIndex;
LLVM_ABI std::string computeTypeName(TypeCollection &Types, TypeIndex Index);
LLVM_ABI StringRef getSymbolName(CVSymbol Sym);
/// For procedure symbols (S_GPROC32, S_LPROC32, etc.), return the linkage name
/// (mangled name) which is the second null-terminated string in the record.
/// For other symbols or if no linkage name exists, falls back to getSymbolName.
LLVM_ABI StringRef getSymbolLinkageName(CVSymbol Sym);
} // namespace codeview
} // namespace llvm

#endif
