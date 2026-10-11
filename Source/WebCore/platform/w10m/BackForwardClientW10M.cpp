// In-memory session history. Moved unchanged from PageW10M.cpp (M10 task 4.2);
// see BackForwardClientW10M.h.
#include "config.h"
#include "BackForwardClientW10M.h"

namespace WebCore {

// Creates an empty history with no current item.
Ref<BackForwardClient> BackForwardClientW10M::create()
{
    return adoptRef(*new BackForwardClientW10M);
}

// Appends after the current item, truncating any forward branch first.
void BackForwardClientW10M::addItem(Ref<HistoryItem>&& item)
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
void BackForwardClientW10M::setChildItem(BackForwardFrameItemIdentifier frameItemID, Ref<HistoryItem>&& item)
{
    for (auto& entry : m_entries) {
        if (entry->frameItemID() == frameItemID) {
            entry->setChildItem(item.copyRef());
            return;
        }
    }
}

// Moves the current pointer to the given entry; unknown items are ignored.
void BackForwardClientW10M::goToItem(HistoryItem& item)
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
Vector<Ref<HistoryItem>> BackForwardClientW10M::allItems(FrameIdentifier)
{
    return m_entries;
}

// Returns the entry at a relative offset; null when out of range.
RefPtr<HistoryItem> BackForwardClientW10M::itemAtIndex(int index, FrameIdentifier)
{
    if (!m_hasCurrent)
        return nullptr;
    if (index < -static_cast<int>(m_current) || index > static_cast<int>(forwardListCount()))
        return nullptr;
    return m_entries[index + m_current].copyRef();
}

// Entries behind the current one; zero before the first navigation.
unsigned BackForwardClientW10M::backListCount() const
{
    return m_hasCurrent ? static_cast<unsigned>(m_current) : 0;
}

// Entries ahead of the current one; zero at the newest navigation.
unsigned BackForwardClientW10M::forwardListCount() const
{
    return m_hasCurrent ? static_cast<unsigned>(m_entries.size() - m_current - 1) : 0;
}

// True when the item belongs to this history.
bool BackForwardClientW10M::containsItem(const HistoryItem& item) const
{
    for (auto& entry : m_entries) {
        if (entry.ptr() == &item)
            return true;
    }
    return false;
}

// Clears every entry and resets the current pointer.
void BackForwardClientW10M::close()
{
    m_entries.clear();
    m_current = 0;
    m_hasCurrent = false;
}

} // namespace WebCore

