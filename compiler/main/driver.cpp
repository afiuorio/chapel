#define EXTERN
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include "chpl.h"
#include "arg.h"
#include "countTokens.h"
#include "driver.h"
#include "files.h"
#include "misc.h"
#include "mysystem.h"
#include "runpasses.h"
#include "stmt.h"
#include "stringutil.h"
#include "version.h"
#include "log.h"
#include "primitive.h"
#include "symbol.h"
#include "config.h"

const char *chplBinaryName = NULL;
FILE* html_index_file = NULL;

char deletedIdFilename[FILENAME_MAX+1] = "";
FILE* deletedIdHandle = NULL;

// for logging
char log_dir[FILENAME_MAX+1] = "./log";
char log_module[FILENAME_MAX+1] = "";
char log_symbol[FILENAME_MAX+1] = "";
bool fLogIds = false;

int currentPassNo = 0;
const char* currentPassName = "starting up";

char CHPL_HOME[FILENAME_MAX] = "";

const char* CHPL_HOST_PLATFORM = NULL;
const char* CHPL_TARGET_PLATFORM = NULL;
const char* CHPL_HOST_COMPILER = NULL;
const char* CHPL_TARGET_COMPILER = NULL;
const char* CHPL_TASKS = NULL;
const char* CHPL_THREADS = NULL;
const char* CHPL_COMM = NULL;
const char* CHPL_COMM_SUBSTRATE = NULL;
const char* CHPL_GASNET_SEGMENT = NULL;
const char* CHPL_ATOMICS = NULL;
const char* CHPL_NETWORK_ATOMICS = NULL;
const char* CHPL_GMP = NULL;
const char* CHPL_WIDE_POINTERS = NULL;
const char* CHPL_MAKE = NULL;

bool widePointersStruct;

int fdump_html = 0;
bool fdump_html_incude_system_modules = true;
static char libraryFilename[FILENAME_MAX] = "";
static char incFilename[FILENAME_MAX] = "";
static char moduleSearchPath[FILENAME_MAX] = "";
static char log_flags[512] = "";
static bool rungdb = false;
bool fLibraryCompile = false;
bool no_codegen = false;
bool genExternPrototypes = false;
int debugParserLevel = 0;
bool developer = false;
bool ignore_errors = false;
bool ignore_warnings = false;
int trace_level = 0;
int fcg = 0;
static bool fBaseline = false;
bool fFastFlag = false;
int fConditionalDynamicDispatchLimit = 0;
bool fNoCopyPropagation = false;
bool fNoDeadCodeElimination = false;
bool fNoScalarReplacement = false;
bool fNoTupleCopyOpt = false;
bool fNoRemoteValueForwarding = false;
bool fNoRemoveCopyCalls = false;
bool fNoOptimizeLoopIterators = false;
bool fNoGlobalConstOpt = false;
bool fNoFastFollowers = false;
bool fNoInlineIterators = false;
bool fNoLiveAnalysis = false;
bool fNoBoundsChecks = false;
bool fNoLocalChecks = false;
bool fNoNilChecks = false;

// flag for llvmWideOpt
bool fLLVMWideOpt = false;

// Enable all extra special warnings
static bool fNoWarnSpecial = true;
static bool fNoWarnDomainLiteral = true;
static bool fNoWarnTupleIteration = true;

bool fNoChecks = false;
bool fNoInline = false;
bool fNoPrivatization = false;
bool fNoOptimizeOnClauses = false;
bool fNoRemoveEmptyRecords = true;
bool fNoRepositionDefExpr = false; // re-initialized in setupOrderedGlobals()
bool fNoInternalModules = false;
int optimize_on_clause_limit = 20;
int scalar_replace_limit = 8;
int tuple_copy_limit = scalar_replace_limit;
bool fGenIDS = false;
bool fSerialForall = false;
bool fSerial;  // initialized in setupOrderedGlobals() below
int fLinkStyle = LS_DEFAULT; // use backend compiler's default
bool fLocal;   // initialized in setupOrderedGlobals() below
bool fHeterogeneous = false; // re-initialized in setupOrderedGlobals() below
bool fieeefloat = true;
bool report_inlining = false;
char fExplainCall[256] = "";
char fExplainInstantiation[256] = "";
bool fPrintCallStackOnError = false;
bool fPrintIDonError = false;
bool fCLineNumbers = false;
char fPrintStatistics[256] = "";
bool fPrintDispatch = false;
bool fReportOptimizedOn = false;
bool fReportPromotion = false;
bool fReportScalarReplace = false;
bool fReportDeadBlocks = false;
bool printCppLineno = false;
bool userSetCppLineno = false;
int num_constants_per_variable = 1;
char defaultDist[256] = "DefaultDist";
int instantiation_limit = 256;
bool fDocs = false;
bool fDocsAlphabetize = false;
char fDocsCommentLabel[256] = "";
char fDocsFolder[256] = "";
bool fDocsTextOnly = false;
char mainModuleName[256] = "";
bool printSearchDirs = false;
bool printModuleFiles = false;
bool llvmCodegen = false;
#ifdef HAVE_LLVM
bool externC = true;
#else
bool externC = false;
#endif
char breakOnCodegenCname[256] = "";

bool debugCCode = false;
bool optimizeCCode = false;

bool fEnableTimers = false;
Timer timer1;
Timer timer2;
Timer timer3;
Timer timer4;
Timer timer5;

bool fNoMemoryFrees = false;
int numGlobalsOnHeap = 0;
bool preserveInlinedLineNumbers = false;

const char* compileCommand = NULL;
char compileVersion[64];

static bool printCopyright = false;
static bool printHelp = false;
static bool printEnvHelp = false;
static bool printSettingsHelp = false;
static bool printLicense = false;
static bool printVersion = false;
static bool printChplHome = false;

/* Note -- LLVM provides a way to get the path to the executable...
// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
llvm::sys::Path GetExecutablePath(const char *Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *MainAddr = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::Path::GetMainExecutable(Argv0, MainAddr);
}
*/

static bool isMaybeChplHome(const char* path)
{
  bool ret = false;
  char* real = dirHasFile(path, "util/chplenv");
  
  if( real ) ret = true;

  free(real);

  return ret;
}

static void setupChplHome(const char* argv0) {
  const char* chpl_home = getenv("CHPL_HOME");
  char* guess = NULL;


  // Get the executable path.
  guess = findProgramPath(argv0);
  if (guess) {
    // Determine CHPL_HOME based on the exe path.
    // Determined exe path, but don't have a env var set
    // Look for ../../../util/chplenv
    // Remove the /bin/some-platform/chpl part
    // from the path.
    if( guess[0] ) {
      int j = strlen(guess) - 5; // /bin and '\0'
      for( ; j >= 0; j-- ) {
        if( guess[j] == '/' &&
            guess[j+1] == 'b' &&
            guess[j+2] == 'i' &&
            guess[j+3] == 'n' ) {
          guess[j] = '\0';
          break;
        }
      }
    }

    if( isMaybeChplHome(guess) ) {
      // OK!
    } else {
      // Maybe we are in e.g. /usr/bin.
      free(guess);
      guess = NULL;
    }
  }

  if( chpl_home ) {
    if( strlen(chpl_home) > FILENAME_MAX )
      USR_FATAL("$CHPL_HOME=%s path too long", chpl_home);

    if( guess == NULL ) {
      // Could not find exe path, but have a env var set
      strncpy(CHPL_HOME, chpl_home, FILENAME_MAX);
    } else {
      // We have env var and found exe path.
      // Check that they match and emit a warning if not.

      if( ! isSameFile(chpl_home, guess) ) {
        // Not the same. Emit warning.
        USR_WARN("$CHPL_HOME=%s mismatched with executable home=%s",
                 chpl_home, guess);
      }
      // Since we have an enviro var, always use that.
      strncpy(CHPL_HOME, chpl_home, FILENAME_MAX);
    }
  } else {
    if( guess == NULL ) {
      // Could not find enviro var, and could not
      // guess at exe's path name.
      USR_FATAL("$CHPL_HOME must be set to run chpl");
    } else {
      int rc;
      
      if( strlen(guess) > FILENAME_MAX )
        USR_FATAL("chpl guessed home %s too long", guess);

      // Determined exe path, but don't have a env var set
      strncpy(CHPL_HOME, guess, FILENAME_MAX);
      // Also need to setenv in this case.
      rc = setenv("CHPL_HOME", guess, 0);
      if( rc ) USR_FATAL("Could not setenv CHPL_HOME");
    }
  }

  // Check that the resulting path is a Chapel distribution.
  if( ! isMaybeChplHome(CHPL_HOME) ) {
    // Bad enviro var.
    USR_WARN("CHPL_HOME=%s is not a Chapel distribution", CHPL_HOME);
  }

  if( guess ) free(guess);

}

static void setCommentLabel(ArgumentState *arg_state, char* label) {
  assert(label != NULL);
  size_t len = strlen(label);
  if (len != 0) {
    if (len > sizeof(fDocsCommentLabel)) {
      USR_FATAL("the label is too large!");
    }else if (label[0] != '/' || label[1] != '*') {
      USR_FATAL("comment label should start with /*");
    } else {
      strcpy(fDocsCommentLabel, label);
    }
  }
}


static const char* setupEnvVar(const char* varname, const char* script) {
  const char* val = runUtilScript(script);
  parseCmdLineConfig(varname, astr("\"", val, "\""));
  return val;
}

#define SETUP_ENV_VAR(varname, script)          \
  varname = setupEnvVar(#varname, script);


//
// Can't rely on a variable initialization order for globals, so any
// variables that need to be initialized in a particular order go here
//
static void setupOrderedGlobals(const char* argv0) {
  // Set up CHPL_HOME first
  setupChplHome(argv0);
  
  // Then CHPL_* variables
  SETUP_ENV_VAR(CHPL_HOST_PLATFORM, "chplenv/platform --host");
  SETUP_ENV_VAR(CHPL_TARGET_PLATFORM, "chplenv/platform --target");
  SETUP_ENV_VAR(CHPL_HOST_COMPILER, "chplenv/compiler --host");
  SETUP_ENV_VAR(CHPL_TARGET_COMPILER, "chplenv/compiler --target");
  SETUP_ENV_VAR(CHPL_TASKS, "chplenv/tasks");
  SETUP_ENV_VAR(CHPL_THREADS, "chplenv/threads");
  SETUP_ENV_VAR(CHPL_COMM, "chplenv/comm");
  SETUP_ENV_VAR(CHPL_COMM_SUBSTRATE, "chplenv/commSubstrate");
  SETUP_ENV_VAR(CHPL_GASNET_SEGMENT, "chplenv/commSegment");
  SETUP_ENV_VAR(CHPL_ATOMICS, "chplenv/atomics");
  SETUP_ENV_VAR(CHPL_NETWORK_ATOMICS, "chplenv/atomics --network");
  SETUP_ENV_VAR(CHPL_GMP, "chplenv/gmp");
  SETUP_ENV_VAR(CHPL_WIDE_POINTERS, "chplenv/widePointers");
  SETUP_ENV_VAR(CHPL_MAKE, "chplenv/chplmake");

  // These depend on the environment variables being set
  fLocal = !strcmp(CHPL_COMM, "none");
  fSerial = !strcmp(CHPL_TASKS, "none"); 
  fNoRepositionDefExpr = strcmp(CHPL_TARGET_PLATFORM, "cray-xmt");
  bool gotPGI = !strcmp(CHPL_TARGET_COMPILER, "pgi")
             || !strcmp(CHPL_TARGET_COMPILER, "cray-prgenv-pgi");
  // conservatively how much is needed for the current PGI compiler
  if (gotPGI) fMaxCIdentLen = 1020;
  
  if( 0 == strcmp(CHPL_WIDE_POINTERS, "struct") ) {
    widePointersStruct = true;
  } else {
    widePointersStruct = false;
  }
}


// NOTE: We are leaking memory here by dropping astr() results on the ground.
static void recordCodeGenStrings(int argc, char* argv[]) {
  compileCommand = astr("chpl ");
  // WARNING: This does not handle arbitrary sequences of escaped characters
  //  in string arguments
  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    // Handle " and \" in strings
    while (char *dq = strchr(arg, '"')) {
      char targ[strlen(argv[i])+4];
      memcpy(targ, arg, dq-arg);
      if ((dq==argv[i]) || ((dq!=argv[i]) && (*(dq-1)!='\\'))) {
        targ[dq-arg] = '\\';
        targ[dq-arg+1] = '"';
        targ[dq-arg+2] = '\0';
      } else {
        targ[dq-arg] = '"';
        targ[dq-arg+1] = '\0';
      }
      arg = dq+1;
      compileCommand = astr(compileCommand, targ);
      if (arg == NULL) break;
    }
    if (arg)
      compileCommand = astr(compileCommand, arg, " ");
  }
  get_version(compileVersion);
}

static void setStaticLink(ArgumentState* arg_state, char* arg_unused) {
  fLinkStyle = LS_STATIC;
}

static void setDynamicLink(ArgumentState* arg_state, char* arg_unused) {
  fLinkStyle = LS_DYNAMIC;
}

static void setChapelDebug(ArgumentState* arg_state, char* arg_unused) {
  printCppLineno = true;
}

static void setDevelSettings(ArgumentState* arg_state, char* arg_unused) {
  // have to handle both cases since this will be called with --devel
  // and --no-devel
  if (developer) {
    ccwarnings = true;
  } else {
    ccwarnings = false;
  }
}

static void 
handleLibrary(ArgumentState* arg_state, char* arg_unused) {
  addLibInfo(astr("-l", libraryFilename));
}

static void 
handleLibPath(ArgumentState* arg_state, char* arg_unused) {
  addLibInfo(astr("-L", libraryFilename));
}

static void handleIncDir(ArgumentState* arg_state, char* arg_unused) {
  addIncInfo(incFilename);
}

static void
compute_program_name_loc(const char* orig_argv0, const char** name, const char** loc) {
  char* argv0 = strdup(orig_argv0);
  char* lastslash = strrchr(argv0, '/');
  if (lastslash == NULL) {
    *name = argv0;
    *loc = findProgramPath(orig_argv0);
  } else {
    *lastslash = '\0';
    *name = lastslash+1;
    *loc = findProgramPath(orig_argv0);
  }
  chplBinaryName = *name;
}


static void runCompilerInGDB(int argc, char* argv[]) {
  const char* gdbCommandFilename = createGDBFile(argc, argv);
  const char* command = astr("gdb -q ", argv[0]," -x ", gdbCommandFilename);
  int status = mysystem(command, "running gdb", 0);

  clean_exit(status);
}


static void readConfig(ArgumentState* arg_state, char* arg_unused) {
  // Expect arg_unused to be a string of either of these forms:
  // 1. name=value -- set the config param "name" to "value"
  // 2. name       -- set the boolean config param "name" to NOT("name")
  //                  if name is not type bool, set it to 0.

  char *name = strdup(arg_unused);
  char *value;
  value = strstr(name, "=");
  if (value) {
    *value = '\0';
    value++;
    if (value[0]) {
      // arg_unused was name=value
      parseCmdLineConfig(name, value);
    } else {
      // arg_unused was name=  <blank>
      USR_FATAL("Missing config param value");
    }
  } else {
    // arg_unused was just name
    parseCmdLineConfig(name, "");
  }
}


static void addModulePath(ArgumentState* arg_state, char* newpath) {
  addFlagModulePath(newpath);
}

static void noteCppLinesSet(ArgumentState* arg, char* unused) {
  userSetCppLineno = true;
}

static void verifySaveCDir(ArgumentState* arg, char* unused) {
  if (saveCDir[0] == '-') {
    USR_FATAL("--savec takes a directory name as its argument\n"
              "       (you specified '%s', assumed to be another flag)",
              saveCDir);
  }
}


static void turnOffChecks(ArgumentState* arg, char* unused) {
  fNoNilChecks = true;
  fNoBoundsChecks = true;
  fNoLocalChecks = true;
}

static void setFastFlag(ArgumentState* arg, char* unused) {
  //
  // Enable all compiler optimizations, disable all runtime checks
  //
  fBaseline = false;
  fieeefloat = false;
  fNoCopyPropagation = false;
  fNoDeadCodeElimination = false;
  fNoFastFollowers = false;
  fNoInline = false;
  fNoInlineIterators = false;
  fNoOptimizeLoopIterators = false;
  fNoLiveAnalysis = false;
  fNoRemoteValueForwarding = false;
  fNoRemoveCopyCalls = false;
  fNoScalarReplacement = false;
  fNoTupleCopyOpt = false;
  fNoPrivatization = false;
  fNoChecks = true;
  fNoBoundsChecks = true;
  fNoLocalChecks = true;
  fNoNilChecks = true;
  fNoOptimizeOnClauses = false;
  optimizeCCode = true;
}

static void setBaselineFlag(ArgumentState* arg, char* unused) {
  //
  // disable all chapel compiler optimizations
  //
  fBaseline = true;
  fNoCopyPropagation = true;
  fNoDeadCodeElimination = true;
  fNoFastFollowers = true;
  fNoInline = true;
  fNoInlineIterators = true;
  fNoLiveAnalysis = true;
  fNoOptimizeLoopIterators = true;
  fNoRemoteValueForwarding = true;
  fNoRemoveCopyCalls = true;
  fNoScalarReplacement = true;
  fNoTupleCopyOpt = true;
  fNoPrivatization = true;
  fNoOptimizeOnClauses = true;
  fConditionalDynamicDispatchLimit = 0;
}

static void setHelpTrue(ArgumentState* arg, char* unused) {
  printHelp = true;
}

static void setHtmlUser(ArgumentState* arg, char* unused) {
  fdump_html = true;
  fdump_html_incude_system_modules = false;
}

static void setWarnTupleIteration(ArgumentState* arg_state, char* unused) {
  const char *val = fNoWarnTupleIteration ? "false" : "true";
  parseCmdLineConfig("CHPL_WARN_TUPLE_ITERATION", astr("\"", val, "\""));
}

static void setWarnDomainLiteral(ArgumentState* arg_state, char* unused) {
  const char *val = fNoWarnDomainLiteral ? "false" : "true";
  parseCmdLineConfig("CHPL_WARN_DOMAIN_LITERAL", astr("\"", val, "\""));
}

static void setWarnSpecial(ArgumentState* arg_state, char* unused) {
  fNoWarnSpecial = false;

  fNoWarnDomainLiteral = false;
  setWarnDomainLiteral(arg_state, unused);

  fNoWarnTupleIteration = false;
  setWarnTupleIteration(arg_state, unused);
}


/*
Flag types:

  I = int
  P = path
  S = string
  D = double
  f = set to false
  F = set to true
  + = increment
  T = toggle
  L = int64 (long)
  N = --no-... flag, --no version sets to false
  n = --no-... flag, --no version sets to true

Record components:
 {"long option" (or "" for separators), 'short option', "description of option argument(s), if any", "option description", "option type", &affectedVariable, "environment variable name", setter_function},
*/

static ArgumentDescription arg_desc[] = {
 {"", ' ', NULL, "Module Processing Options", NULL, NULL, NULL, NULL},
 {"count-tokens", ' ', NULL, "[Don't] count tokens in main modules", "N", &countTokens, "CHPL_COUNT_TOKENS", NULL},
 {"main-module", ' ', "<module>", "Specify entry point module", "S256", mainModuleName, NULL, NULL},
 {"module-dir", 'M', "<directory>", "Add directory to module search path", "P", moduleSearchPath, NULL, addModulePath},
 {"print-code-size", ' ', NULL, "[Don't] print code size of main modules", "N", &printTokens, "CHPL_PRINT_TOKENS", NULL},
 {"print-module-files", ' ', NULL, "Print module file locations", "F", &printModuleFiles, NULL, NULL},
 {"print-search-dirs", ' ', NULL, "[Don't] print module search path", "N", &printSearchDirs, "CHPL_PRINT_SEARCH_DIRS", NULL},

 {"", ' ', NULL, "Parallelism Control Options", NULL, NULL, NULL, NULL},
 {"local", ' ', NULL, "Target one [many] locale[s]", "N", &fLocal, "CHPL_LOCAL", NULL},
 {"serial", ' ', NULL, "[Don't] Serialize parallel constructs", "N", &fSerial, "CHPL_SERIAL", NULL},
 {"serial-forall", ' ', NULL, "[Don't] Serialize forall constructs", "N", &fSerialForall, "CHPL_SERIAL_FORALL", NULL},

 {"", ' ', NULL, "Optimization Control Options", NULL, NULL, NULL, NULL},
 {"baseline", ' ', NULL, "Disable all Chapel optimizations", "F", &fBaseline, "CHPL_BASELINE", setBaselineFlag},
 {"conditional-dynamic-dispatch-limit", ' ', "<limit>", "Set limit on # of inline conditionals used for dynamic dispatch", "I", &fConditionalDynamicDispatchLimit, "CHPL_CONDITIONAL_DYNAMIC_DISPATCH_LIMIT", NULL},
 {"copy-propagation", ' ', NULL, "Enable [disable] copy propagation", "n", &fNoCopyPropagation, "CHPL_DISABLE_COPY_PROPAGATION", NULL},
 {"dead-code-elimination", ' ', NULL, "Enable [disable] dead code elimination", "n", &fNoDeadCodeElimination, "CHPL_DISABLE_DEAD_CODE_ELIMINATION", NULL},
 {"fast", ' ', NULL, "Use fast default settings", "F", &fFastFlag, NULL, setFastFlag},
 {"fast-followers", ' ', NULL, "Enable [disable] fast followers", "n", &fNoFastFollowers, "CHPL_DISABLE_FAST_FOLLOWERS", NULL},
 {"ieee-float", ' ', NULL, "Generate code that is strict [lax] with respect to IEEE compliance", "N", &fieeefloat, "CHPL_IEEE_FLOAT", NULL},
 {"inline", ' ', NULL, "Enable [disable] function inlining", "n", &fNoInline, NULL, NULL},
 {"inline-iterators", ' ', NULL, "Enable [disable] iterator inlining", "n", &fNoInlineIterators, "CHPL_DISABLE_INLINE_ITERATORS", NULL},
 {"live-analysis", ' ', NULL, "Enable [disable] live variable analysis", "n", &fNoLiveAnalysis, "CHPL_DISABLE_LIVE_ANALYSIS", NULL},
 {"optimize-loop-iterators", ' ', NULL, "Enable [disable] optimization of iterators composed of a single loop", "n", &fNoOptimizeLoopIterators, "CHPL_DISABLE_OPTIMIZE_LOOP_ITERATORS", NULL},
 {"optimize-on-clauses", ' ', NULL, "Enable [disable] optimization of on clauses", "n", &fNoOptimizeOnClauses, "CHPL_DISABLE_OPTIMIZE_ON_CLAUSES", NULL},
 {"optimize-on-clause-limit", ' ', "<limit>", "Limit recursion depth of on clause optimization search", "I", &optimize_on_clause_limit, "CHPL_OPTIMIZE_ON_CLAUSE_LIMIT", NULL},
 {"privatization", ' ', NULL, "Enable [disable] privatization of distributed arrays and domains", "n", &fNoPrivatization, "CHPL_DISABLE_PRIVATIZATION", NULL},
 {"remove-copy-calls", ' ', NULL, "Enable [disable] remove copy calls", "n", &fNoRemoveCopyCalls, "CHPL_DISABLE_REMOVE_COPY_CALLS", NULL},
 {"remote-value-forwarding", ' ', NULL, "Enable [disable] remote value forwarding", "n", &fNoRemoteValueForwarding, "CHPL_DISABLE_REMOTE_VALUE_FORWARDING", NULL},
 {"scalar-replacement", ' ', NULL, "Enable [disable] scalar replacement", "n", &fNoScalarReplacement, "CHPL_DISABLE_SCALAR_REPLACEMENT", NULL},
 {"scalar-replace-limit", ' ', "<limit>", "Limit on the size of tuples being replaced during scalar replacement", "I", &scalar_replace_limit, "CHPL_SCALAR_REPLACE_TUPLE_LIMIT", NULL},
 {"tuple-copy-opt", ' ', NULL, "Enable [disable] tuple (memcpy) optimization", "n", &fNoTupleCopyOpt, "CHPL_DISABLE_TUPLE_COPY_OPT", NULL},
 {"tuple-copy-limit", ' ', "<limit>", "Limit on the size of tuples considered for optimization", "I", &tuple_copy_limit, "CHPL_TUPLE_COPY_LIMIT", NULL},
 
 {"", ' ', NULL, "Run-time Semantic Check Options", NULL, NULL, NULL, NULL},
 {"no-checks", ' ', NULL, "Disable all following run-time checks", "F", &fNoChecks, "CHPL_NO_CHECKS", turnOffChecks},
 {"bounds-checks", ' ', NULL, "Enable [disable] bounds checking", "n", &fNoBoundsChecks, "CHPL_NO_BOUNDS_CHECKING", NULL},
 {"local-checks", ' ', NULL, "Enable [disable] local block checking", "n", &fNoLocalChecks, NULL, NULL},
 {"nil-checks", ' ', NULL, "Enable [disable] nil checking", "n", &fNoNilChecks, "CHPL_NO_NIL_CHECKS", NULL},

 {"", ' ', NULL, "C Code Generation Options", NULL, NULL, NULL, NULL},
 {"codegen", ' ', NULL, "[Don't] Do code generation", "n", &no_codegen, "CHPL_NO_CODEGEN", NULL},
 {"cpp-lines", ' ', NULL, "[Don't] Generate #line annotations", "N", &printCppLineno, "CHPL_CG_CPP_LINES", noteCppLinesSet},
 {"max-c-ident-len", ' ', NULL, "Maximum length of identifiers in generated code, 0 for unlimited", "I", &fMaxCIdentLen, "CHPL_MAX_C_IDENT_LEN", NULL},
 {"savec", ' ', "<directory>", "Save generated C code in directory", "P", saveCDir, "CHPL_SAVEC_DIR", verifySaveCDir},

 {"", ' ', NULL, "C Code Compilation Options", NULL, NULL, NULL, NULL},
 {"ccflags", ' ', "<flags>", "Back-end C compiler flags", "S256", ccflags, "CHPL_CC_FLAGS", NULL},
 {"debug", 'g', NULL, "[Don't] Support debugging of generated C code", "N", &debugCCode, "CHPL_DEBUG", setChapelDebug},
 {"dynamic", ' ', NULL, "Generate a dynamically linked binary", "F", &fLinkStyle, NULL, setDynamicLink},
 {"hdr-search-path", 'I', "<directory>", "C header search path", "P", incFilename, NULL, handleIncDir},
 {"ldflags", ' ', "<flags>", "Back-end C linker flags", "S256", ldflags, "CHPL_LD_FLAGS", NULL},
 {"lib-linkage", 'l', "<library>", "C library linkage", "P", libraryFilename, "CHPL_LIB_NAME", handleLibrary},
 {"lib-search-path", 'L', "<directory>", "C library search path", "P", libraryFilename, "CHPL_LIB_PATH", handleLibPath},
 {"make", ' ', "<make utility>", "Make utility for generated code", "S256", &CHPL_MAKE, "CHPL_MAKE", NULL},
 {"optimize", 'O', NULL, "[Don't] Optimize generated C code", "N", &optimizeCCode, "CHPL_OPTIMIZE", NULL},
 {"output", 'o', "<filename>", "Name output executable", "P", executableFilename, "CHPL_EXE_NAME", NULL},
 {"static", ' ', NULL, "Generate a statically linked binary", "F", &fLinkStyle, NULL, setStaticLink},

 {"", ' ', NULL, "LLVM Code Generation Options", NULL, NULL, NULL, NULL},
 {"llvm", ' ', NULL, "[Don't] use the LLVM code generator", "N", &llvmCodegen, "CHPL_LLVM_CODEGEN", NULL},
 {"llvm-wide-opt", ' ', NULL, "Enable [disable] LLVM wide pointer optimizations", "N", &fLLVMWideOpt, "CHPL_LLVM_WIDE_OPTS", NULL},

 {"", ' ', NULL, "Documentation Options", NULL, NULL, NULL, NULL},
 {"docs", ' ', NULL, "Runs documentation on the source file", "N", &fDocs, "CHPL_DOC", NULL },
 {"docs-alphabetical", ' ', NULL, "Alphabetizes the documentation", "N", &fDocsAlphabetize, NULL, NULL},
 {"docs-comment-style", ' ', "<indicator>", "Only includes comments that start with <indicator>", "S256", fDocsCommentLabel, NULL, setCommentLabel},
 {"docs-dir", ' ', "<dirname>", "Sets the documentation directory to <dirname>", "S256", fDocsFolder, NULL, NULL},
 {"docs-text-only", ' ', NULL, "Generate text only documentation", "F", &fDocsTextOnly, NULL, NULL},

 {"", ' ', NULL, "Compilation Trace Options", NULL, NULL, NULL, NULL},
 {"print-commands", ' ', NULL, "[Don't] print system commands", "N", &printSystemCommands, "CHPL_PRINT_COMMANDS", NULL},
 {"print-passes", ' ', NULL, "[Don't] print compiler passes", "N", &printPasses, "CHPL_PRINT_PASSES", NULL},

 {"", ' ', NULL, "Miscellaneous Options", NULL, NULL, NULL, NULL},
// Support for extern { c-code-here } blocks could be toggled with this
// flag, but instead we just leave it on if the compiler can do it.
// {"extern-c", ' ', NULL, "Enable [disable] extern C block support", "f", &externC, "CHPL_EXTERN_C", NULL},
 {"devel", ' ', NULL, "Compile as a developer [user]", "N", &developer, "CHPL_DEVELOPER", setDevelSettings},
 {"explain-call", ' ', "<call>[:<module>][:<line>]", "Explain resolution of call", "S256", fExplainCall, NULL, NULL},
 {"explain-instantiation", ' ', "<function|type>[:<module>][:<line>]", "Explain instantiation of type", "S256", fExplainInstantiation, NULL, NULL},
 {"instantiate-max", ' ', "<max>", "Limit number of instantiations", "I", &instantiation_limit, "CHPL_INSTANTIATION_LIMIT", NULL},
 {"print-callstack-on-error", ' ', NULL, "print the Chapel call stack leading to each error or warning", "N", &fPrintCallStackOnError, "CHPL_PRINT_CALLSTACK_ON_ERROR", NULL},
 {"set", 's', "<name>[=<value>]", "Set config param value", "S", NULL, NULL, readConfig},
 {"warn-special", ' ', NULL, "Enable [disable] special warnings", "n", &fNoWarnSpecial, "CHPL_WARN_SPECIAL", setWarnSpecial},
 {"warn-domain-literal", ' ', NULL, "Enable [disable] old domain literal syntax warnings", "n", &fNoWarnDomainLiteral, "CHPL_WARN_DOMAIN_LITERAL", setWarnDomainLiteral},
 {"warn-tuple-iteration", ' ', NULL, "Enable [disable] warnings for tuple iteration", "n", &fNoWarnTupleIteration, "CHPL_WARN_TUPLE_ITERATION", setWarnTupleIteration},
 {"no-warnings", ' ', NULL, "Disable output of warnings", "F", &ignore_warnings, "CHPL_DISABLE_WARNINGS", NULL},

 {"", ' ', NULL, "Compiler Information Options", NULL, NULL, NULL, NULL},
 {"copyright", ' ', NULL, "Show copyright", "F", &printCopyright, NULL, NULL},
 {"help", 'h', NULL, "Help (show this list)", "F", &printHelp, NULL, NULL},
 {"help-env", ' ', NULL, "Environment variable help", "F", &printEnvHelp, "", setHelpTrue},
 {"help-settings", ' ', NULL, "Current flag settings", "F", &printSettingsHelp, "", setHelpTrue},
 {"license", ' ', NULL, "Show license", "F", &printLicense, NULL, NULL},
 {"version", ' ', NULL, "Show version", "F", &printVersion, NULL, NULL},

 {"", ' ', NULL, "Developer Flags -- Debug Output", NULL, NULL, NULL, NULL},
 {"cc-warnings", ' ', NULL, "[Don't] Give warnings for generated code", "N", &ccwarnings, "CHPL_CC_WARNINGS", NULL},
 {"c-line-numbers", ' ', NULL, "Use C code line numbers and filenames", "F", &fCLineNumbers, NULL, NULL},
 {"gen-extern-prototypes", ' ', NULL, "[Don't] generate C prototypes for extern declarations", "N", &genExternPrototypes, "CHPL_GEN_EXTERN_PROTOTYPES", NULL},
 {"gen-ids", ' ', NULL, "[Don't] pepper generated code with BaseAST::ids", "N", &fGenIDS, "CHPL_GEN_IDS", NULL},
 {"html", 't', NULL, "Dump IR in HTML format (toggle)", "T", &fdump_html, "CHPL_HTML", NULL},
 {"html-user", ' ', NULL, "Dump IR in HTML for user module(s) only (toggle)", "T", &fdump_html, NULL, setHtmlUser},
 {"log", 'd', "<letters>", "Dump IR in text format. See log.h for definition of <letters>. Empty argument (\"-d=\" or \"--log=\") means \"log all passes\"", "S512", log_flags, "CHPL_LOG_FLAGS", log_flags_arg},
 {"log-dir", ' ', "<path>", "Specify log directory", "P", log_dir, "CHPL_LOG_DIR", NULL},
 {"log-ids", ' ', NULL, "[Don't] include BaseAST::ids in log files", "N", &fLogIds, "CHPL_LOG_IDS", NULL},
 {"log-module", ' ', "<module-name>", "Restrict IR dump to the named module", "S256", log_module, "CHPL_LOG_MODULE", NULL},
// {"log-symbol", ' ', "<symbol-name>", "Restrict IR dump to the named symbol(s)", "S256", log_symbol, "CHPL_LOG_SYMBOL", NULL}, // This doesn't work yet.
 {"parser-debug", 'D', NULL, "Set parser debug level", "+", &debugParserLevel, "CHPL_PARSER_DEBUG", NULL},
 {"print-dispatch", ' ', NULL, "Print dynamic dispatch table", "F", &fPrintDispatch, NULL, NULL},
 {"print-statistics", ' ', "[n|k|t]", "Print AST statistics", "S256", fPrintStatistics, NULL, NULL},
 {"report-inlining", ' ', NULL, "Print inlined functions", "F", &report_inlining, NULL, NULL},
 {"report-dead-blocks", ' ', NULL, "Print dead block removal stats", "F", &fReportDeadBlocks, NULL, NULL},
 {"report-optimized-on", ' ', NULL, "Print information about on clauses that have been optimized for potential fast remote fork operation", "F", &fReportOptimizedOn, NULL, NULL},
 {"report-promotion", ' ', NULL, "Print information about scalar promotion", "F", &fReportPromotion, NULL, NULL},
 {"report-scalar-replace", ' ', NULL, "Print scalar replacement stats", "F", &fReportScalarReplace, NULL, NULL},

 {"", ' ', NULL, "Developer Flags -- Miscellaneous", NULL, NULL, NULL, NULL},
 {"break-on-id", ' ', NULL, "Break when AST id is created", "I", &breakOnID, "CHPL_BREAK_ON_ID", NULL},
 {"break-on-delete-id", ' ', NULL, "Break when AST id is deleted", "I", &breakOnDeleteID, "CHPL_BREAK_ON_DELETE_ID", NULL},
 {"break-on-codegen", ' ', NULL, "Break when function cname is code generated", "S256", &breakOnCodegenCname, "CHPL_LLVM_CODEGEN", NULL},
 {"default-dist", ' ', "<distribution>", "Change the default distribution", "S256", defaultDist, "CHPL_DEFAULT_DIST", NULL},
 {"gdb", ' ', NULL, "Run compiler in gdb", "F", &rungdb, NULL, NULL},
 {"heterogeneous", ' ', NULL, "Compile for heterogeneous nodes", "F", &fHeterogeneous, "", NULL},
 {"ignore-errors", ' ', NULL, "[Don't] attempt to ignore errors", "N", &ignore_errors, "CHPL_IGNORE_ERRORS", NULL},
 {"library", ' ', NULL, "Generate a Chapel library file", "F", &fLibraryCompile, NULL, NULL},
 {"localize-global-consts", ' ', NULL, "Enable [disable] optimization of global constants", "n", &fNoGlobalConstOpt, "CHPL_DISABLE_GLOBAL_CONST_OPT", NULL},
 {"local-temp-names", ' ', NULL, "[Don't] Generate locally-unique temp names", "N", &localTempNames, "CHPL_LOCAL_TEMP_NAMES", NULL},
 {"log-deleted-ids-to", ' ', "<filename>", "Log AST id and memory address of each deleted node to the specified file", "P", deletedIdFilename, "CHPL_DELETED_ID_FILENAME", NULL},
 {"memory-frees", ' ', NULL, "Enable [disable] memory frees in the generated code", "n", &fNoMemoryFrees, "CHPL_DISABLE_MEMORY_FREES", NULL},
 {"preserve-inlined-line-numbers", ' ', NULL, "[Don't] Preserve file names/line numbers in inlined code", "N", &preserveInlinedLineNumbers, "CHPL_PRESERVE_INLINED_LINE_NUMBERS", NULL},
 {"print-id-on-error", ' ', NULL, "[Don't] print AST id in error messages", "N", &fPrintIDonError, "CHPL_PRINT_ID_ON_ERROR", NULL},
 {"remove-empty-records", ' ', NULL, "Enable [disable] empty record removal", "n", &fNoRemoveEmptyRecords, "CHPL_DISABLE_REMOVE_EMPTY_RECORDS", NULL},
 {"reposition-def-expressions", ' ', NULL, "Enable [disable] repositioning of def expressions to usage points", "n", &fNoRepositionDefExpr, "CHPL_DISABLE_REPOSITION_DEF_EXPR", NULL},
 {"ignore-internal-modules", ' ', NULL, "Enable [disable] skipping internal module initialization in generated code (for testing)", "N", &fNoInternalModules, "CHPL_DISABLE_INTERNAL_MODULES", NULL},
 {"timers", ' ', NULL, "[Don't] Enable general timers one to five", "N", &fEnableTimers, "CHPL_ENABLE_TIMERS", NULL},
 {"print-chpl-home", ' ', NULL, "Print CHPL_HOME and path to this executable and exit", "F", &printChplHome, NULL, NULL},
 {0}
};


static ArgumentState arg_state = {
  0, 0,
  "program", 
  "path",
  arg_desc
};


static void setupDependentVars(void) {
  if (developer && !userSetCppLineno) {
    printCppLineno = false;
  }
}


static void printStuff(const char* argv0) {
  bool shouldExit = false;
  bool printedSomething = false;

  if (printVersion) {
    fprintf(stdout, "%s Version %s\n", arg_state.program_name, compileVersion);
    printCopyright = true;
    printedSomething = true;
    shouldExit = true;
  }
  if (printLicense) {
    fprintf(stdout,
#include "LICENSE"
            );
    printCopyright = false;
    shouldExit = true;
    printedSomething = true;
  }
  if (printCopyright) {
    fprintf(stdout,
#include "COPYRIGHT"
            );
    printedSomething = true;
  }
  if( printChplHome ) {
    char* guess = findProgramPath(argv0);
    printf("%s\t%s\n", CHPL_HOME, guess);
    free(guess);
    printedSomething = true;
  }

  if (printHelp || (!printedSomething && arg_state.nfile_arguments < 1)) {
    if (printedSomething) printf("\n");
    usage(&arg_state, (!printHelp), printEnvHelp, printSettingsHelp);
    shouldExit = true;
    printedSomething = true;
  }
  if (printedSomething && arg_state.nfile_arguments < 1) {
    shouldExit = true;
  }
  if (shouldExit) {
    clean_exit(0);
  }
}


static void
compile_all(void) {
  testInputFiles(arg_state.nfile_arguments, arg_state.file_argument);
  runPasses();
}

int main(int argc, char *argv[]) {
  startCatchingSignals();
 {
  astlocMarker markAstLoc(0, "<internal>");
  initFlags();
  initChplProgram();
  initPrimitive();
  initPrimitiveTypes();
  initTheProgram();
  setupOrderedGlobals(argv[0]);
  compute_program_name_loc(argv[0], &(arg_state.program_name),
                           &(arg_state.program_loc));
  process_args(&arg_state, argc, argv);
  initCompilerGlobals(); // must follow argument parsing
  setupDependentVars();
  setupModulePaths();
  recordCodeGenStrings(argc, argv);
 } // astlocMarker scope
  printStuff(argv[0]);
  if (rungdb)
    runCompilerInGDB(argc, argv);
  if (fdump_html || strcmp(log_flags, ""))
    init_logs();
  compile_all();
  if (fEnableTimers) {
    printf("timer 1: %8.3lf\n", timer1.elapsed());
    printf("timer 2: %8.3lf\n", timer2.elapsed());
    printf("timer 3: %8.3lf\n", timer3.elapsed());
    printf("timer 4: %8.3lf\n", timer4.elapsed());
    printf("timer 5: %8.3lf\n", timer5.elapsed());
  }
  free_args(&arg_state);
  clean_exit(0);
  return 0;
}
