#include "lld/Common/Driver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

LLD_HAS_DRIVER(macho)

extern "C" int mako_lld_macho_main(int argc, const char *const *argv) {
  const lld::DriverDef driver{lld::Darwin, &lld::macho::link};
  const llvm::ArrayRef<const char *> args(argv, static_cast<size_t>(argc));
  const llvm::ArrayRef<lld::DriverDef> drivers(&driver, 1);
  const lld::Result result =
      lld::lldMain(args, llvm::outs(), llvm::errs(), drivers);
  if (!result.canRunAgain)
    return result.retCode == 0 ? 70 : result.retCode;
  return result.retCode;
}
