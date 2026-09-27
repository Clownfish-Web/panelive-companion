import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Services.SystemTray
import qs.Commons
import qs.Ui

Panel {
  id: root
  moduleName: "panelive-companion"
  ipcTarget: "panelive-companion"

  readonly property var companion: {
    var items = SystemTray.items.values
    for (var i = 0; i < items.length; ++i)
      if (items[i].id === "panelive-companion") return items[i]
    return null
  }
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color muted: Qt.rgba(foreground.r, foreground.g, foreground.b, 0.58)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  visible: companion !== null
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight
  onCompanionChanged: if (!companion) close()
  property bool confirmingShutdown: false
  property int selectedAction: 0
  property bool keyboardCursor: false
  onOpenedChanged: {
    confirmingShutdown = false
    selectedAction = 0
    keyboardCursor = false
  }

  function requestShutdown() {
    if (!shutdownAction || !shutdownAction.enabled) return
    if (!confirmingShutdown) {
      confirmingShutdown = true
      selectedAction = 0
      if (keyboardCursor) focusAction(0)
    }
    else {
      shutdownAction.triggered()
      close()
    }
  }

  function focusAction(index) {
    selectedAction = index === 1 && shutdownButton.enabled ? 1 : 0
    keyboardCursor = true
    if (selectedAction === 0) openButton.forceActiveFocus()
    else shutdownButton.forceActiveFocus()
    Qt.callLater(function() {
      var bottom = actionRow.y + actionRow.height
      if (bottom > details.contentY + details.height)
        details.contentY = Math.min(Math.max(0, details.contentHeight - details.height),
                                   bottom - details.height)
      else if (actionRow.y < details.contentY) details.contentY = actionRow.y
    })
  }

  function activateAction() {
    if (selectedAction === 1) requestShutdown()
    else if (confirmingShutdown) confirmingShutdown = false
    else openCompanion()
  }

  QsMenuOpener {
    id: companionMenu
    menu: root.companion ? root.companion.menu : null
  }

  readonly property var shutdownAction: {
    var entries = companionMenu.children.values
    for (var i = 0; i < entries.length; ++i)
      if (entries[i].text === "Shutdown Companion") return entries[i]
    return null
  }

  function openCompanion() {
    if (companion) companion.activate()
    close()
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: "PaneLive Companion status"
    Accessible.onPressAction: root.toggle()
    Keys.onReturnPressed: root.toggle()
    Keys.onSpacePressed: root.toggle()
    active: root.companion && root.companion.status === Status.NeedsAttention
    tooltipText: "PaneLive Companion status"
    onPressed: function(code) {
      if (code === Qt.RightButton) root.openCompanion()
      else root.toggle()
    }
    iconComponent: Component {
      Image {
        source: root.companion ? root.companion.icon : ""
        fillMode: Image.PreserveAspectFit
      }
    }
  }

  KeyboardPanel {
    id: popup
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: details
    contentWidth: popup.fittedContentWidth(Style.space(400))
    contentHeight: popup.fittedContentHeight(content.implicitHeight, Style.space(560))

    PanelKeyCatcher {
      id: keys
      anchors.fill: parent
      onCloseRequested: {
        if (root.confirmingShutdown) {
          root.confirmingShutdown = false
          root.focusAction(0)
        } else root.close()
      }
      onMoveRequested: function(dx, dy) {
        root.focusAction(Math.max(0, Math.min(1, root.selectedAction + (dx || dy))))
      }
      onActivateRequested: root.activateAction()
      onTabRequested: function(direction) {
        var next = root.keyboardCursor ? (root.selectedAction + direction + 2) % 2
                                       : (direction > 0 ? 0 : 1)
        root.focusAction(next)
      }
      Flickable {
        id: details
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Keys.onPressed: function(event) {
          if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
            var delta = event.key === Qt.Key_PageDown ? height : -height
            contentY = Math.max(0, Math.min(contentHeight - height, contentY + delta))
            event.accepted = true
          }
        }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
          id: content
          width: parent.width
          spacing: Style.space(16)
          PanelHero {
            title: "PaneLive Companion"
            meta: "Production monitoring"
            foreground: root.foreground
            fontFamily: root.fontFamily
            iconComponent: Component {
              Image {
                source: root.companion ? root.companion.icon : ""
                width: Style.space(36)
                height: width
                fillMode: Image.PreserveAspectFit
              }
            }
          }
          Rectangle {
            width: parent.width
            height: 1
            color: root.foreground
            opacity: 0.25
          }
          Column {
            width: parent.width
            Repeater {
              model: root.companion
                ? String(root.companion.tooltipDescription || "Status: Pending").split("\n")
                : ["Companion: Not running"]
              delegate: Item {
                required property string modelData
                readonly property int delimiter: modelData.indexOf(":")
                width: content.width
                implicitHeight: Math.max(label.implicitHeight, value.implicitHeight) + Style.space(20)
                Text {
                  id: label
                  anchors.left: parent.left
                  anchors.verticalCenter: parent.verticalCenter
                  width: (parent.width - Style.space(18)) * 0.52
                  text: delimiter >= 0 ? modelData.slice(0, delimiter) : modelData
                  textFormat: Text.StyledText
                  wrapMode: Text.Wrap
                  color: root.muted
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.bodySmall
                }
                Text {
                  id: value
                  anchors.right: parent.right
                  anchors.verticalCenter: parent.verticalCenter
                  width: (parent.width - Style.space(18)) * 0.48
                  text: delimiter >= 0 ? modelData.slice(delimiter + 1).trim() : ""
                  textFormat: Text.StyledText
                  wrapMode: Text.Wrap
                  horizontalAlignment: Text.AlignRight
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.bodySmall
                  font.weight: Font.Medium
                }
                Rectangle {
                  anchors.left: parent.left
                  anchors.right: parent.right
                  anchors.bottom: parent.bottom
                  height: 1
                  color: root.foreground
                  opacity: 0.10
                }
              }
            }
          }
          Text {
            visible: root.confirmingShutdown
            width: parent.width
            text: "Disconnect all clients and stop Companion?"
            wrapMode: Text.Wrap
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
          }
          Row {
            id: actionRow
            width: parent.width
            spacing: Style.space(10)
            Button {
              id: openButton
              text: root.confirmingShutdown ? "Cancel" : "Open Companion"
              width: (parent.width - parent.spacing) / 2
              bordered: true
              focusable: true
              Accessible.role: Accessible.Button
              Accessible.name: text
              Accessible.onPressAction: clicked()
              onActiveFocusChanged: if (activeFocus) root.selectedAction = 0
              hasCursor: root.keyboardCursor && root.selectedAction === 0
              onHovered: function(hovered) {
                if (hovered) { root.selectedAction = 0; root.keyboardCursor = false }
              }
              onClicked: {
                if (root.confirmingShutdown) root.confirmingShutdown = false
                else root.openCompanion()
              }
            }
            Button {
              id: shutdownButton
              text: root.confirmingShutdown ? "Confirm shutdown" : "Shutdown Companion"
              width: (parent.width - parent.spacing) / 2
              bordered: true
              focusable: true
              Accessible.role: Accessible.Button
              Accessible.name: text
              Accessible.onPressAction: clicked()
              onActiveFocusChanged: if (activeFocus) root.selectedAction = 1
              foreground: root.bar ? root.bar.urgent : Color.urgent
              enabled: root.shutdownAction !== null && root.shutdownAction.enabled
              hasCursor: root.keyboardCursor && root.selectedAction === 1
              onHovered: function(hovered) {
                if (hovered) { root.selectedAction = 1; root.keyboardCursor = false }
              }
              onClicked: root.requestShutdown()
            }
          }
        }
      }
    }
  }
}
