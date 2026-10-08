// In-memory session history. Moved unchanged from PageUWP.cpp (M10 task 4.2);
// see BackForwardClientUWP.h.
#include "config.h"
#include "BackForwardClientUWP.h"

namespace WebCore {

// Creates an empty history with no current item.
Ref<BackForwardClient> BackForwardClientUWP::create()
{
    return adoptRef(*new BackForwardClientUWP);
}

// Appends after the current item, truncating any forward branch first.
void BackForwardClientUWP::addItem(Ref<HistoryItem>&& item)
{
    if (m_hasCurrent) {
        while (m_entries.size() > m_current + 1)
            m_entries.takeLast();
    }

    // Oldest entries fall off; history never grows without bound.
    constexpr size_t kMaxHistoryEntries = 100;
    if (m_entries.size() >= kMaxHistoryEntries) {
        m_entries.removeAt(0);
        if (m_hasCurrent && m_current > 0)
            --m_current;
    }
    if (!m_hasCurrent) {
        m_entries.append(WTF::move(item));
        m_current = 0;
    } else {
        m_entries.insert(m_current + 1, WTF::move(item));
        ++m_current;
    }
    m_hasCurrent = true;
}

// Replaces the child item of the matching frame entry.
void BackForwardClientUWP::setChildItem(BackForwardFrameItemIdentifier frameItemID, Ref<HistoryItem>&& item)
{
    for (auto& entry : m_entries) {
        if (entry->frameItemID() == frameItemID) {
            entry->setChildItem(item.copyRef());
            return;
        }
    }
}

// Moves the current pointer to the given entry; unknown items are ignored.
void BackForwardClientUWP::goToItem(HistoryItem& item)
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].ptr() == &item) {
            m_current = i;
            m_hasCurrent = true;
            return;
        }
    }
}

// Returns every entry for history traversal queries.
Vector<Ref<HistoryItem>> BackForwardClientUWP::allItems(FrameIdentifier)
{
    return m_entries;
}

// Returns the entry at a relative offset; null when out of range.
RefPtr<HistoryItem> BackForwardClientUWP::itemAtIndex(int index, FrameIdentifier)
{
    if (!m_hasCurrent)
        return nullptr;
    if (index < -static_cast<int>(m_current) || index > static_cast<int>(forwardListCount()))
        return nullptr;
    return m_entries[index + m_current].copyRef();
}

// Entries behind the current one; zero before the first navigation.
unsigned BackForwardClientUWP::backListCount() const
{
    return m_hasCurrent ? static_cast<unsigned>(m_current) : 0;
}

// Entries ahead of the current one; zero at the newest navigation.
unsigned BackForwardClientUWP::forwardListCount() const
{
    return m_hasCurrent ? static_cast<unsigned>(m_entries.size() - m_current - 1) : 0;
}

// True when the item belongs to this history.
bool BackForwardClientUWP::containsItem(const HistoryItem& item) const
{
    for (auto& entry : m_entries) {
        if (entry.ptr() == &item)
            return true;
    }
    return false;
}

// Clears every entry and resets the current pointer.
void BackForwardClientUWP::close()
{
    m_entries.clear();
    m_current = 0;
    m_hasCurrent = false;
}

} // namespace WebCore
