//===- llvm/Object/BuildID.cpp - Build ID ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a library for handling Build IDs and using them to find
/// debug info.
///
//===----------------------------------------------------------------------===//

#include "llvm/Object/BuildID.h"

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/CVDebugRecord.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace llvm::object;

namespace {

template <typename ELFT> BuildIDRef getBuildID(const ELFFile<ELFT> &Obj) {
  auto findBuildID = [&Obj](const auto &ShdrOrPhdr,
                            uint64_t Alignment) -> std::optional<BuildIDRef> {
    Error Err = Error::success();
    for (auto N : Obj.notes(ShdrOrPhdr, Err))
      if (N.getType() == ELF::NT_GNU_BUILD_ID &&
          N.getName() == ELF::ELF_NOTE_GNU)
        return N.getDesc(Alignment);
    consumeError(std::move(Err));
    return std::nullopt;
  };

  auto Sections = cantFail(Obj.sections());
  for (const auto &S : Sections) {
    if (S.sh_type != ELF::SHT_NOTE)
      continue;
    if (std::optional<BuildIDRef> ShdrRes = findBuildID(S, S.sh_addralign))
      return ShdrRes.value();
  }
  auto PhdrsOrErr = Obj.program_headers();
  if (!PhdrsOrErr) {
    consumeError(PhdrsOrErr.takeError());
    return {};
  }
  for (const auto &P : *PhdrsOrErr) {
    if (P.p_type != ELF::PT_NOTE)
      continue;
    if (std::optional<BuildIDRef> PhdrRes = findBuildID(P, P.p_align))
      return PhdrRes.value();
  }
  return {};
}

} // namespace

// Helper function to extract GUID + Age from COFF debug directory
static BuildID getCOFFBuildID(const COFFObjectFile *Obj) {
  for (const auto &DebugDir : Obj->debug_directories()) {
    if (DebugDir.Type != COFF::IMAGE_DEBUG_TYPE_CODEVIEW)
      continue;
      
    const codeview::DebugInfo *DebugInfo;
    StringRef PDBFileName;
    if (auto EC = Obj->getDebugPDBInfo(&DebugDir, DebugInfo, PDBFileName))
      continue;
      
    // Check if this is PDB 7.0 format (RSDS)
    if (DebugInfo->PDB70.CVSignature == OMF::Signature::PDB70) {
      BuildID Result;
      Result.reserve(20); // 16 bytes GUID + 4 bytes Age
      
      // Add the 16-byte GUID
      const uint8_t *GuidBytes = DebugInfo->PDB70.Signature;
      Result.append(GuidBytes, GuidBytes + 16);
      
      // Add the 4-byte Age (little-endian)
      uint32_t Age = DebugInfo->PDB70.Age;
      Result.push_back(Age & 0xFF);
      Result.push_back((Age >> 8) & 0xFF);
      Result.push_back((Age >> 16) & 0xFF);
      Result.push_back((Age >> 24) & 0xFF);
      
      return Result;
    }
  }
  return {};
}

BuildID llvm::object::parseBuildID(StringRef Str) {
  std::string Bytes;
  if (!tryGetFromHex(Str, Bytes))
    return {};
  ArrayRef<uint8_t> BuildID(reinterpret_cast<const uint8_t *>(Bytes.data()),
                            Bytes.size());
  return SmallVector<uint8_t>(BuildID);
}

BuildIDRef llvm::object::getBuildID(const ObjectFile *Obj) {
  if (auto *O = dyn_cast<ELFObjectFile<ELF32LE>>(Obj))
    return ::getBuildID(O->getELFFile());
  if (auto *O = dyn_cast<ELFObjectFile<ELF32BE>>(Obj))
    return ::getBuildID(O->getELFFile());
  if (auto *O = dyn_cast<ELFObjectFile<ELF64LE>>(Obj))
    return ::getBuildID(O->getELFFile());
  if (auto *O = dyn_cast<ELFObjectFile<ELF64BE>>(Obj))
    return ::getBuildID(O->getELFFile());
  return {};
}

BuildID llvm::object::getCOFFDebugID(const ObjectFile *Obj) {
  if (auto *COFFObj = dyn_cast<COFFObjectFile>(Obj))
    return getCOFFBuildID(COFFObj);
  return {};
}

std::optional<std::string> BuildIDFetcher::fetch(BuildIDRef BuildID) const {
  auto GetDebugPath = [&](StringRef Directory) {
    SmallString<128> Path{Directory};
    sys::path::append(Path, ".build-id",
                      llvm::toHex(BuildID[0], /*LowerCase=*/true),
                      llvm::toHex(BuildID.slice(1), /*LowerCase=*/true));
    Path += ".debug";
    return Path;
  };
  if (DebugFileDirectories.empty()) {
    SmallString<128> Path = GetDebugPath(
#if defined(__NetBSD__)
        // Try /usr/libdata/debug/.build-id/../...
        "/usr/libdata/debug"
#else
        // Try /usr/lib/debug/.build-id/../...
        "/usr/lib/debug"
#endif
    );
    if (llvm::sys::fs::exists(Path))
      return std::string(Path);
  } else {
    for (const auto &Directory : DebugFileDirectories) {
      // Try <debug-file-directory>/.build-id/../...
      SmallString<128> Path = GetDebugPath(Directory);
      if (llvm::sys::fs::exists(Path))
        return std::string(Path);
    }
  }
  return std::nullopt;
}
