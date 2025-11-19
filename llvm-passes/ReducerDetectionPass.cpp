// LLVM Pass to detect if lambdas passed to forall capture RAJA::Reducer or mfem reducers
//
// This pass only checks forall functions that are annotated with:
//   [[clang::annotate("check_reducer")]]
//
// Build with:
//   clang++ -shared -fPIC ReducerDetectionPass.cpp -o ReducerDetectionPass.so \
//     `llvm-config --cxxflags --ldflags`
//
// Run with:
//   opt -load-pass-plugin=./ReducerDetectionPass.so -passes=reducer-detection \
//     -disable-output input.ll

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Constants.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>
#include <string>

using namespace llvm;

namespace {

class ReducerDetectionPass : public PassInfoMixin<ReducerDetectionPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    bool foundReducer = false;

    errs() << "=== RAJA/MFEM Reducer Detection Pass ===\n";
    errs() << "Only checking annotated forall functions with [[clang::annotate(\"check_reducer\")]]\n\n";

    // Build set of annotated functions
    buildAnnotatedFunctions(M);

    if (annotatedFunctions.empty()) {
      errs() << "No annotated forall functions found.\n";
      errs() << "To enable checking, annotate your forall template with:\n";
      errs() << "  [[clang::annotate(\"check_reducer\")]]\n\n";
      return PreservedAnalyses::all();
    }

    errs() << "Found " << annotatedFunctions.size() << " annotated function(s) to check\n\n";

    // Find all forall-related function calls
    for (Function &F : M) {
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (auto *Call = dyn_cast<CallInst>(&I)) {
            Function *CalledFunc = Call->getCalledFunction();
            if (CalledFunc && isAnnotatedForall(CalledFunc)) {
              errs() << "Checking annotated forall: " << CalledFunc->getName() << "\n";
              errs() << "  Called from: " << F.getName() << "\n";

              // Check the lambda argument (usually first arg after N)
              if (analyzeLambdaForReducer(Call)) {
                foundReducer = true;
              }
              errs() << "\n";
            }
          }
        }
      }
    }

    if (!foundReducer) {
      errs() << "No reducers detected in annotated forall lambdas.\n";
    }

    return PreservedAnalyses::all();
  }

private:
  std::set<const Function*> annotatedFunctions;

  // Build set of functions that have the "check_reducer" annotation
  void buildAnnotatedFunctions(Module &M) {
    // Check for llvm.global.annotations
    GlobalVariable *Annos = M.getNamedGlobal("llvm.global.annotations");
    if (!Annos) return;

    ConstantArray *CA = dyn_cast<ConstantArray>(Annos->getInitializer());
    if (!CA) return;

    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      ConstantStruct *CS = dyn_cast<ConstantStruct>(CA->getOperand(i));
      if (!CS || CS->getNumOperands() < 2) continue;

      // First operand is the annotated value (function)
      Value *Annotated = CS->getOperand(0);
      if (auto *Func = dyn_cast<Function>(Annotated->stripPointerCasts())) {
        // Second operand is the annotation string
        if (auto *AnnotationGEP = dyn_cast<ConstantExpr>(CS->getOperand(1))) {
          if (auto *AnnotationVar = dyn_cast<GlobalVariable>(
                AnnotationGEP->getOperand(0))) {
            if (auto *AnnotationData = dyn_cast<ConstantDataArray>(
                  AnnotationVar->getInitializer())) {
              StringRef Annotation = AnnotationData->getAsCString();
              if (Annotation == "check_reducer") {
                annotatedFunctions.insert(Func);
                errs() << "  Found annotated function: " << Func->getName() << "\n";
              }
            }
          }
        }
      }
    }
  }

  // Check if this function is annotated for reducer checking
  bool isAnnotatedForall(const Function *F) {
    return annotatedFunctions.count(F) > 0;
  }

  // Analyze the lambda argument to see if it captures a Reducer
  bool analyzeLambdaForReducer(CallInst *Call) {
    bool foundReducer = false;

    // Check all arguments to the call
    for (unsigned i = 0; i < Call->arg_size(); ++i) {
      Value *Arg = Call->getArgOperand(i);
      Type *ArgType = Arg->getType();

      errs() << "  Analyzing argument " << i << ": ";
      ArgType->print(errs());
      errs() << "\n";

      // For lambda closures, the type will be a struct/class
      if (auto *StructTy = dyn_cast<StructType>(ArgType->getPointerElementType())) {
        errs() << "    Lambda struct type: " << StructTy->getName() << "\n";

        if (structContainsReducer(StructTy)) {
          errs() << "    *** REDUCER DETECTED in lambda capture! ***\n";
          foundReducer = true;
        }
      }

      // Also check if passed by value (not pointer)
      if (auto *StructTy = dyn_cast<StructType>(ArgType)) {
        errs() << "    Lambda struct type (by value): " << StructTy->getName() << "\n";

        if (structContainsReducer(StructTy)) {
          errs() << "    *** REDUCER DETECTED in lambda capture! ***\n";
          foundReducer = true;
        }
      }
    }

    return foundReducer;
  }

  // Recursively check if a struct contains a Reducer type
  bool structContainsReducer(StructType *ST) {
    std::set<StructType*> visited;
    return structContainsReducerImpl(ST, visited);
  }

  bool structContainsReducerImpl(StructType *ST, std::set<StructType*> &visited) {
    if (!ST || visited.count(ST)) {
      return false;
    }
    visited.insert(ST);

    StringRef typeName = ST->getName();

    // Check for RAJA::Reducer patterns
    if (typeName.contains("RAJA") && typeName.contains("Reducer")) {
      errs() << "      Found RAJA::Reducer: " << typeName << "\n";
      return true;
    }

    // Check for mfem reducer patterns
    if (typeName.contains("mfem")) {
      if (typeName.contains("SumReducer") ||
          typeName.contains("MinReducer") ||
          typeName.contains("MaxReducer") ||
          typeName.contains("MultReducer") ||
          typeName.contains("BAndReducer") ||
          typeName.contains("BOrReducer") ||
          typeName.contains("MinMaxReducer") ||
          typeName.contains("ArgMinReducer") ||
          typeName.contains("ArgMaxReducer") ||
          typeName.contains("ArgMinMaxReducer")) {
        errs() << "      Found mfem Reducer: " << typeName << "\n";
        return true;
      }
    }

    // Check for generic "Reducer" suffix
    if (typeName.endswith("Reducer") || typeName.contains("Reducer<")) {
      errs() << "      Found Reducer type: " << typeName << "\n";
      return true;
    }

    // Recursively check all member types
    for (Type *ElementType : ST->elements()) {
      // Handle nested structs
      if (auto *NestedStruct = dyn_cast<StructType>(ElementType)) {
        if (structContainsReducerImpl(NestedStruct, visited)) {
          return true;
        }
      }

      // Handle pointers to structs
      if (auto *PtrType = dyn_cast<PointerType>(ElementType)) {
        if (auto *NestedStruct = dyn_cast<StructType>(PtrType->getPointerElementType())) {
          if (structContainsReducerImpl(NestedStruct, visited)) {
            return true;
          }
        }
      }

      // Handle arrays of structs
      if (auto *ArrayType = dyn_cast<ArrayType>(ElementType)) {
        if (auto *NestedStruct = dyn_cast<StructType>(ArrayType->getElementType())) {
          if (structContainsReducerImpl(NestedStruct, visited)) {
            return true;
          }
        }
      }
    }

    return false;
  }
};

} // anonymous namespace

// Register the pass
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "ReducerDetectionPass", LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "reducer-detection") {
            MPM.addPass(ReducerDetectionPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}
