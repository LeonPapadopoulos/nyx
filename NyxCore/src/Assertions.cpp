#include "NyxCorePCH.h"
#include "Assertions.h"
#include "Log.h"

void reportAssertionFailure(const char* expr, const char* file, int line)
{
	CORE_LOG_CRITICAL(expr);
}
