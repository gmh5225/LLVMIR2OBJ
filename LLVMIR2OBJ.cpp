#include <direct.h>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"

using namespace llvm;

bool startsWith(const std::string &s, const std::string &prefix) {
  return (s.size() >= prefix.size() &&
          s.compare(0, prefix.size(), prefix) == 0);
}

auto split(const std::string &s, char delim, size_t max = -1) {
  auto start = 0;
  std::vector<std::string> res;
  while (start != std::string::npos && (max == -1 || res.size() < (max - 1))) {
    auto pos = s.find(delim);
    auto sub = (pos == std::string::npos) ? s.substr(start)
                                          : s.substr(start, pos - start);
    res.push_back(sub);
    start = (pos == std::string::npos) ? pos : ++pos;
  }
  if (start != std::string::npos)
    res.push_back(s.substr(start));
  return res;
}

typedef std::map<std::string, std::vector<std::string>> CommandLine;
auto parseCommandLine(int argc, char *argv[]) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; i++)
    args.push_back(argv[i]);

  CommandLine cl;
  for (auto arg : args) {
    auto isSwitch = startsWith(arg, "--");
    if (isSwitch) {
      auto parts = split(arg, '=', 2);
      auto key = (parts.size() == 2) ? parts[0] : arg;
      cl[key] = cl[key];
      if (parts.size() == 2)
        cl[key].push_back(parts[1]);
    } else
      cl[""].push_back(arg);
  }

  return cl;
}

const char *UsageString =
    "Usage: LLVMIR2OBJ.exe inputfile.ll/bc outputfile.obj\n";

int main(int argc, char *argv[]) {
  int Ret = 0;

  do {
    auto CmdLine = parseCommandLine(argc, argv);
    auto Args = CmdLine[""];
    if (Args.size() != 3) {
      std::cout << UsageString << std::endl;
      Ret = -1;
      break;
    }

    auto InputIRFile = Args[1];
    auto OutputObjFile = Args[2];

    SMDiagnostic Err;
    LLVMContext CTX;
    auto Module = parseIRFile(InputIRFile, Err, CTX);
    if (!Module.get()) {
      Err.print("", errs());
      Ret = -2;
      break;
    }

    auto &M = *(Module.get());
    auto StrTriple = M.getTargetTriple();
    Triple TheTriple;
    TheTriple.setTriple(StrTriple);
    std::cout << "Triple:" << StrTriple << std::endl;

    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    InitializeAllAsmParsers();

    auto &DataLayout = M.getDataLayout();
    auto PointerSize = DataLayout.getPointerSize();

    std::string Error;
    auto TheTarget = TargetRegistry::lookupTarget(StrTriple, Error);
    if (!TheTarget) {
      std::cout << "Gen original obj failed! Error:" << Error << "\n";
      Ret = -3;
      break;
    }

    // create target machine
    auto RM = Optional<Reloc::Model>();
    TargetOptions Options;
    auto Target = std::unique_ptr<TargetMachine>(
        TheTarget->createTargetMachine(StrTriple, "", "", Options, RM));
    if (!Target.get()) {
      std::cout
          << "Gen original obj failed! Error: Failed to create target machine"
          << "\n";
      Ret = -4;
      break;
    }

    std::error_code ECC;
    sys::fs::OpenFlags OpenFlags = sys::fs::OF_None;
    std::unique_ptr<ToolOutputFile> Out =
        std::make_unique<ToolOutputFile>(OutputObjFile, ECC, OpenFlags);
    if (!Out.get()) {
      std::cout << "Gen original obj failed! Error: Failed to create OutputFile"
                << "\n";

      Ret = -5;
      break;
    }

    // output
    legacy::PassManager PM;
    raw_pwrite_stream *OS = &Out->os();
    if (Target->addPassesToEmitFile(PM, *OS, nullptr,
                                    CodeGenFileType::CGFT_ObjectFile)) {
      std::cout << "Gen original obj failed! Error: Failed to emit obj"
                << "\n";

      Ret = -6;
      break;
    }

    PM.run(M);
    Out->keep();

    Ret = 1;
  } while (0);

  return Ret;
}
