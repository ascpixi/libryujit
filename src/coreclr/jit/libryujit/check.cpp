#include "../../inc/check.inl"

BOOL CHECK::s_neverEnforceAsserts = 0;

#ifdef _DEBUG

#include <sstring.h>

LONG CHECK::t_count;

void CHECK::Setup(LPCSTR message, LPCSTR condition, LPCSTR file, INT line)
{
    STATIC_CONTRACT_NOTHROW;
    STATIC_CONTRACT_GC_NOTRIGGER;
    STATIC_CONTRACT_SUPPORTS_DAC_HOST_ONLY;

    //
    // It might be nice to collect all of the message here.  But for now, we will just
    // retain the innermost one.
    //

    if (m_message == NULL)
    {
        m_message   = message;
        m_condition = condition;
        m_file      = file;
        m_line      = line;
    }
}

void CHECK::Trigger(LPCSTR reason)
{
    STATIC_CONTRACT_NOTHROW;
    STATIC_CONTRACT_GC_NOTRIGGER;

    // TODO: We also could log m_message here
    ryujit_host_panic(reason);
}

#endif
