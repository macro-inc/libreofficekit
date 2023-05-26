#pragma once

#include "rtl/ustring.hxx"

namespace macro_internal {
inline constexpr OUStringLiteral TERM_URL = u"term://";
inline constexpr OUStringLiteral TERM_REF_URL = u"termref://";
inline constexpr OUStringLiteral SECTION_URL = u"section://";
inline constexpr OUStringLiteral SECTION_REF_URL = u"sectionref://";

/// Given a url this will return true IFF the url is a predefined custom url used by Macro for overlays
bool isTemporaryURL(const OUString& url);
}
