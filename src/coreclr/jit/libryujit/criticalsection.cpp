#include "../../inc/crsttypes_generated.h"
#include "../../pal/prebuilt/inc/clrinternal.h"

#include "./host.h"

typedef void* CRITSEC_COOKIE;

CRITSEC_COOKIE ClrCreateCriticalSection(CrstType type, CrstFlags flags) { return (CRITSEC_COOKIE)ryujit_host_create_lock(); }
void ClrDeleteCriticalSection(CRITSEC_COOKIE cookie) { ryujit_host_delete_lock((void*)cookie); }
void ClrEnterCriticalSection(CRITSEC_COOKIE cookie) { ryujit_host_enter_lock((void*)cookie); }
void ClrLeaveCriticalSection(CRITSEC_COOKIE cookie) { ryujit_host_exit_lock((void*)cookie); }
