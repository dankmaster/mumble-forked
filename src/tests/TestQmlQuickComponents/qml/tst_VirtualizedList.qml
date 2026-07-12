import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "VirtualizedList"
    when: windowShown
    width: 520
    height: 420
    property int createdDelegates: 0
    property int destroyedDelegates: 0

    ListModel {
        id: rows
    }
    ListView {
        id: list
        anchors.fill: parent
        model: rows
        clip: true
        reuseItems: true
        cacheBuffer: 160
        delegate: Rectangle {
            required property int index
            required property string body
            required property int rowHeight
            width: list.width
            height: rowHeight
            Component.onCompleted: ++testCase.createdDelegates
            Component.onDestruction: ++testCase.destroyedDelegates
        }
    }

    function populate(count) {
        rows.clear();
        const batch = [];
        for (let i = 0; i < count; ++i)
            batch.push({
                body: "Message " + i,
                rowHeight: 32 + (i % 4) * 8
            });
        rows.append(batch);
    }

    function init() {
        createdDelegates = 0;
        destroyedDelegates = 0;
        populate(10000);
        wait(0);
    }

    function test_10000_rows_remain_virtualized_and_reused() {
        compare(rows.count, 10000);
        verify(createdDelegates < 100, "ListView created " + createdDelegates + " delegates");
        list.positionViewAtIndex(9000, ListView.Beginning);
        wait(0);
        verify(createdDelegates < 160, "Delegate reuse stopped at " + createdDelegates);
    }

    function test_prepend_preserves_visual_anchor() {
        list.positionViewAtIndex(500, ListView.Beginning);
        wait(10);
        const anchorIndex = 500;
        const anchorItem = list.itemAtIndex(anchorIndex);
        verify(anchorItem !== null);
        const anchorBody = rows.get(anchorIndex).body;
        const oldY = anchorItem.mapToItem(list, 0, 0).y;
        const prepended = [];
        for (let i = 0; i < 25; ++i)
            prepended.push({
                body: "Earlier " + i,
                rowHeight: 40
            });
        for (let i = prepended.length - 1; i >= 0; --i)
            rows.insert(0, prepended[i]);
        wait(10);
        const newIndex = anchorIndex + 25;
        compare(rows.get(newIndex).body, anchorBody);
        compare(anchorItem.index, newIndex);
        compare(anchorItem.body, anchorBody);
        const newY = anchorItem.mapToItem(list, 0, 0).y;
        fuzzyCompare(newY, oldY, 1.0);
    }

    function test_dynamic_height_preserves_anchor() {
        list.positionViewAtIndex(750, ListView.Beginning);
        wait(10);
        const anchorIndex = 750;
        const nextIndex = anchorIndex + 1;
        const nextItem = list.itemAtIndex(nextIndex);
        verify(nextItem !== null);
        const oldY = nextItem.mapToItem(list, 0, 0).y;
        const oldHeight = rows.get(anchorIndex).rowHeight;
        rows.setProperty(anchorIndex, "rowHeight", oldHeight + 72);
        wait(10);
        const shiftedItem = list.itemAtIndex(nextIndex);
        verify(shiftedItem !== null);
        const shiftedY = shiftedItem.mapToItem(list, 0, 0).y;
        fuzzyCompare(shiftedY - oldY, 72, 1.0);
    }
}
