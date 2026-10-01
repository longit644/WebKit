// WebKitWebView PublicSuffixStore stub (no libpsl on UWP).
// Naive last-two-labels fallback: safe degradation, cookies keep working,
// over-broad suffix matching accepted until a real PSL ships.
#include "config.h"
#include "PublicSuffixStore.h"

namespace WebCore {

bool PublicSuffixStore::platformIsPublicSuffix(StringView) const
{
    return false;
}

String PublicSuffixStore::platformTopPrivatelyControlledDomain(StringView domain) const
{
    auto utf8 = domain.utf8();
    const char* data = utf8.data();
    if (!data || !data[0])
        return String();
    // Skip leading dots (cookie format), then keep the last two labels.
    size_t start = 0;
    while (data[start] == '.')
        ++start;
    size_t end = start;
    while (data[end])
        ++end;
    if (start == end)
        return String();
    size_t dots = 0;
    size_t cut = start;
    for (size_t i = end; i > start; --i) {
        if (data[i - 1] == '.') {
            ++dots;
            if (dots == 2) {
                cut = i;
                break;
            }
        }
    }
    return String::fromLatin1(data + cut);
}

} // namespace WebCore
