#include "macro_internal.hxx"
#include "rtl/ustring.hxx"

bool macro_internal::isTemporaryURL(const OUString& url)
{
    return url.startsWith(macro_internal::TERM_URL)
        || url.startsWith(macro_internal::TERM_REF_URL)
        || url.startsWith(macro_internal::SECTION_URL)
        || url.startsWith(macro_internal::SECTION_REF_URL);
}
