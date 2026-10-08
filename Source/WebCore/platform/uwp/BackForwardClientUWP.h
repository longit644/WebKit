#pragma once

#include "BackForwardClient.h"
#include "HistoryItem.h"
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>

// In-memory session history for one page. Moved from PageUWP.cpp in task 4.2.
namespace WebCore {

// Appending truncates the forward branch and the list is capped, so
// Back/Forward/Reload work without persistent storage. Unchanged from
// PageUWP.cpp so history keeps its baseline before any behavioral change.
class BackForwardClientUWP final : public BackForwardClient {
public:
    // Creates an empty history with no current item.
    static Ref<BackForwardClient> create();

private:
    // Appends after the current item, truncating any forward branch first.
    void addItem(Ref<HistoryItem>&& item) final;

    // Replaces the child item of the matching frame entry.
    void setChildItem(BackForwardFrameItemIdentifier frameItemID, Ref<HistoryItem>&& item) final;

    // Moves the current pointer to the given entry; unknown items are ignored.
    void goToItem(HistoryItem& item) final;

    // Returns every entry for history traversal queries.
    Vector<Ref<HistoryItem>> allItems(FrameIdentifier) final;

    // Returns the entry at a relative offset; null when out of range.
    RefPtr<HistoryItem> itemAtIndex(int index, FrameIdentifier) final;

    // Entries behind the current one; zero before the first navigation.
    unsigned backListCount() const final;

    // Entries ahead of the current one; zero at the newest navigation.
    unsigned forwardListCount() const final;

    // True when the item belongs to this history.
    bool containsItem(const HistoryItem& item) const final;

    // Clears every entry and resets the current pointer.
    void close() final;

    // Ordered entries, oldest first.
    Vector<Ref<HistoryItem>> m_entries;

    // Index of the current entry within the list above.
    size_t m_current { 0 };

    // False until the first navigation lands.
    bool m_hasCurrent { false };
};

} // namespace WebCore
