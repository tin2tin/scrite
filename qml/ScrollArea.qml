/****************************************************************************
**
** Copyright (C) TERIFLIX Entertainment Spaces Pvt. Ltd. Bengaluru
** Author: Prashanth N Udupa (prashanth.udupa@teriflix.com)
**
** This code is distributed under GPL v3. Complete text of the license
** can be found here: https://www.gnu.org/licenses/gpl-3.0.txt
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

import QtQml 2.13
import QtQuick 2.13
import QtQuick.Controls 2.13

import Scrite 1.0

Flickable {
    id: scrollAreaFlickable
    property rect visibleRect: Qt.rect(contentX, contentY, width, height)
    property real initialContentWidth: 100
    property real initialContentHeight: 100
    property real suggestedScale: zoomScale
    property alias handlePinchZoom: pinchHandler.enabled
    property bool showScrollBars: true
    property bool zoomOnScroll: app.isWindowsPlatform || app.isLinuxPlatform
    property bool animatePanAndZoom: true
    property alias minimumScale: pinchHandler.minimumScale
    property alias maximumScale: pinchHandler.maximumScale
    FlickScrollSpeedControl.factor: workspaceSettings.flickScrollSpeedFactor

    boundsBehavior: Flickable.StopAtBounds
    clip: true

    property bool changing: false
    TrackerPack {
        delay: 250

        TrackProperty {
            target: scrollAreaFlickable
            property: "contentX"
            onTracked: scrollAreaFlickable.changing = true
        }
        TrackProperty {
            target: scrollAreaFlickable
            property: "contentY"
            onTracked: scrollAreaFlickable.changing = true
        }
        TrackProperty {
            target: scrollAreaFlickable
            property: "moving"
            onTracked: scrollAreaFlickable.changing = true
        }
        TrackProperty {
            target: scrollAreaFlickable
            property: "flicking"
            onTracked: scrollAreaFlickable.changing = true
        }

        onTracked: scrollAreaFlickable.changing = false
    }

    signal zoomScaleChangedInteractively()

    function zoomIn() {
        zoomScale = Math.min(zoomScale*(1+scrollAreaSettings.zoomFactor), pinchHandler.maximumScale)
    }

    function zoomOut() {
        zoomScale = Math.max(zoomScale*(1-scrollAreaSettings.zoomFactor), pinchHandler.minimumScale)
    }

    function zoomOne() {
        zoomScale = 1
    }

    function zoomTo(val) {
        zoomScale = val
    }

    function zoomFit(area) {
        if(!area)
            return

        if(area.width <= 0 || area.height <= 0)
            return

        var s = Math.min(width/area.width, height/area.height)
        var center = Qt.point(area.x + area.width/2, area.y + area.height/2)
        var w = width/s
        var h = height/s
        area = Qt.rect(Math.max(center.x-w/2,0), Math.max(center.y-h/2,0), w, h)

        zoomScale = s
        ensureVisible(area, s)
    }

    property bool animatingPanOrZoom: contentXAnimation.running || contentYAnimation.running || zoomScaleAnimation.running
    Behavior on contentX {
        enabled: screenplayEditorSettings.enableAnimations && animatePanAndZoom
        NumberAnimation { id: contentXAnimation; duration: 250 }
    }
    Behavior on contentY {
        enabled: screenplayEditorSettings.enableAnimations && animatePanAndZoom
        NumberAnimation { id: contentYAnimation; duration: 250 }
    }
    Behavior on zoomScale {
        id: zoomScaleBehavior
        property bool allow: true
        enabled: screenplayEditorSettings.enableAnimations && animatePanAndZoom && allow
        NumberAnimation { id: zoomScaleAnimation; duration: 250 }
    }

    ScrollBar.horizontal: ScrollBar2 { flickable: scrollAreaFlickable }
    ScrollBar.vertical: ScrollBar2 { flickable: scrollAreaFlickable }

    function ensureItemVisible(item, scaling, leaveMargin) {
        if(item === null)
            return
        var area = Qt.rect(item.x, item.y, item.width, item.height)
        ensureVisible(area, scaling, leaveMargin)
    }

    property var ensureVisibleParams

    function ensureVisible(area, scaling, leaveMargin) {
        if(leaveMargin === undefined)
            leaveMargin = 20
        if(scaling === undefined)
            scaling = 1

        area = Qt.rect( area.x*scaling, area.y*scaling,
                        area.width*scaling, area.height*scaling )

        if(area.right > contentWidth || area.bottom > contentHeight || width < 0 || height < 0) {
            ensureVisibleParams = {
                "area": area, "scaling": scaling, "leaveMargin": leaveMargin
            }
            app.execLater(scrollAreaFlickable, 500, function() {
                var params = ensureVisibleParams
                ensureVisibleParams = undefined
                ensureVisible(params.area, params.scaling, params.leaveMargin)
            })
            return
        }

        // Check if the areaangle can be contained within the viewport
        if(area.width > visibleRect.width || area.height > visibleRect.height) {
            // We are here if area cannot fit into the space of the flickable.
            // In this case, we just try and fit the center point of area into the
            // view.
            var w = width*0.2
            var h = height*0.2
            area = Qt.rect( (area.left+area.right)/2-h/2,
                            (area.top+area.bottom)/2-w/2,
                            w, h )
        }

        // Check if item is already visible
        if(area.left >= visibleRect.left && area.top >= visibleRect.top &&
           area.right <= visibleRect.right && area.bottom <= visibleRect.bottom)
            return; // already visible

        var cx = undefined
        var cy = undefined
        if(area.left >= visibleRect.right || area.right >= visibleRect.right)
            cx = (area.right + leaveMargin) - width
        else if(area.right <= visibleArea.left || area.left <= visibleRect.left)
            cx = area.left - leaveMargin

        if(area.top >= visibleRect.bottom || area.bottom >= visibleRect.bottom)
            cy = (area.bottom + leaveMargin) - height
        else if(area.bottom <= visibleRect.top || area.top <= visibleRect.top)
            cy = area.top - leaveMargin

        if(cx !== undefined)
            contentX = Math.max(Math.min(cx, contentWidth-width-1),0)
        if(cy !== undefined)
            contentY = Math.max(Math.min(cy, contentHeight-height-1),0)
    }

    function ensureVisibleFast(area) {
        // Check if item is already visible
        if(area.left >= visibleRect.left && area.top >= visibleRect.top &&
           area.right <= visibleRect.right && area.bottom <= visibleRect.bottom)
            return; // already visible

        var cx = undefined
        var cy = undefined
        if(area.left >= visibleRect.right || area.right >= visibleRect.right)
            cx = area.right - width
        else if(area.right <= visibleArea.left || area.left <= visibleRect.left)
            cx = area.left

        if(area.top >= visibleRect.bottom || area.bottom >= visibleRect.bottom)
            cy = area.bottom - height
        else if(area.bottom <= visibleRect.top || area.top <= visibleRect.top)
            cy = area.top

        if(cx !== undefined)
            contentX = Math.max(Math.min(cx, contentWidth-width-1),0)
        if(cy !== undefined)
            contentY = Math.max(Math.min(cy, contentHeight-height-1),0)
    }

    property real zoomScale: 1

    onZoomScaleChanged: {
        var cursorPos = app.cursorPosition()
        var fCursorPos = app.mapGlobalPositionToItem(scrollAreaFlickable, cursorPos)
        var fContainsCursor = fCursorPos.x >= 0 && fCursorPos.y >= 0 && fCursorPos.x <= width && fCursorPos.y <= height
        var visibleArea = Qt.rect(contentX, contentY, width, height)
        var mousePoint = fContainsCursor ?
                    app.mapGlobalPositionToItem(contentItem, app.cursorPosition()) :
                    Qt.point(contentX+width/2, contentY+height/2)
        var newWidth = initialContentWidth * zoomScale
        var newHeight = initialContentHeight * zoomScale
        resizeContent(newWidth, newHeight, mousePoint)
        returnToBoundsTimer.start()
    }

    PinchHandler {
        id: pinchHandler
        target: null
        onTargetChanged: target = null

        minimumScale: Math.min(parent.scale, 0.25)
        maximumScale: Math.max(4, parent.scale)
        minimumRotation: 0
        maximumRotation: 0
        minimumPointCount: 2

        onScaleChanged: {
            if(scrollAreaFlickable === null)
                return
            zoomScaleBehavior.allow = false
            zoomScale = activeScale
            zoomScaleChangedInteractively()
            zoomScaleBehavior.allow = true
        }
    }

    EventFilter.active: zoomOnScroll
    EventFilter.events: [31]
    EventFilter.onFilter: {
        if(event.delta < 0)
            zoomOut()
        else
            zoomIn()
        zoomScaleChangedInteractively()
        result.acceptEvent = true
        result.filter = true
    }

    Timer {
        id: returnToBoundsTimer
        objectName: "ScrollArea.returnToBoundsTimer"
        running: false
        repeat: false
        interval: 500
        onTriggered: parent.returnToBounds()
    }
}
