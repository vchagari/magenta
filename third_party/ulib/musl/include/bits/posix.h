#if defined(__x86_64__)
#include "../../arch/x86_64/bits/posix.h"
#elif defined(__aarch64__)
#include "../../arch/aarch64/bits/posix.h"
#elif defined(__arm__)
#include "../../arch/arm/bits/posix.h"
#else
#error Unsupported architecture!
#endif