import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

Window {
    id: root
    objectName: "settingsWindow"
    
    flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    
    width: 440
    height: 580
    
    required property AppFacade appFacade
    
    readonly property var settings: appFacade.settings
    
    Rectangle {
        id: contentRect
        anchors.fill: parent
        color: Theme.surfaceColor
        radius: Theme.radiusLarge
        border.color: Theme.borderColor
        border.width: 1
        focus: true
        
        Keys.onEscapePressed: {
            root.close();
        }
        
        // Title Bar
        Rectangle {
            id: titleBar
            width: parent.width
            height: 48
            color: "transparent"
            
            Text {
                text: qsTr("设置")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                anchors.centerIn: parent
            }
            
            StyleButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing12
                anchors.verticalCenter: parent.verticalCenter
                iconSource: "qrc:/qt/qml/Seriona/qml/assets/close.svg"
                buttonWidth: 28
                buttonHeight: 28
                iconSize: 12
                textColor: Theme.textSecondary
                onClicked: {
                    root.close();
                }
            }
            
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 40 // Don't overlap close button
                onPressed: {
                    root.startSystemMove();
                }
            }
        }
        
        Rectangle {
            id: divider
            width: parent.width
            height: 1
            anchors.top: titleBar.bottom
            color: Theme.borderColor
        }
        
        // Content Area
        Flickable {
            id: flickable
            anchors.top: divider.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true
            contentWidth: width
            contentHeight: contentLayout.implicitHeight + Theme.spacing24 * 2
            
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: Theme.scrollbarWidth
                background: Rectangle {
                    color: "transparent"
                }
                contentItem: Rectangle {
                    radius: Theme.radiusSmall
                    color: parent.hovered ? Theme.scrollbarHoverColor : Theme.scrollbarColor
                }
            }
            
            ColumnLayout {
                id: contentLayout
                width: parent.width - Theme.spacing24 * 2
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Theme.spacing16
                spacing: Theme.spacing16

                // ==========================================
                // Card 1: 音频输出配置 (Audio Output Card)
                // ==========================================
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: audioCardLayout.implicitHeight + Theme.spacing16 * 2
                    color: Theme.raisedSurfaceColor
                    radius: Theme.radiusMedium
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: audioCardLayout
                        anchors.fill: parent
                        anchors.margins: Theme.spacing16
                        spacing: Theme.spacing16

                        Text {
                            text: qsTr("音频输出")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }

                        // Row 1: Output Mode
                        RowLayout {
                            id: outputModeRow
                            objectName: "outputModeGroup"
                            Layout.fillWidth: true
                            
                            Text {
                                text: qsTr("输出模式")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                                Layout.preferredWidth: 100
                            }
                            
                            ButtonGroup {
                                id: modeGroup
                            }
                            
                            Rectangle {
                                Layout.preferredWidth: 200
                                Layout.preferredHeight: 32
                                color: Theme.surfaceColor
                                radius: Theme.radiusSmall
                                border.color: Theme.borderSubtle
                                border.width: 1
                                
                                Row {
                                    anchors.fill: parent
                                    spacing: 0
                                    
                                    Button {
                                        id: directOutputBtn
                                        width: parent.width / 2
                                        height: parent.height
                                        checkable: true
                                        checked: settings.outputMode === 0
                                        ButtonGroup.group: modeGroup
                                        
                                        background: Rectangle {
                                            radius: Theme.radiusSmall
                                            color: directOutputBtn.checked ? Theme.accentColor : "transparent"
                                            
                                            Behavior on color {
                                                ColorAnimation { duration: Theme.animationFast }
                                            }
                                        }
                                        
                                        contentItem: Text {
                                            text: qsTr("直接输出")
                                            color: directOutputBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                            font.pixelSize: Theme.fontBody
                                            font.weight: directOutputBtn.checked ? Font.DemiBold : Font.Normal
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        
                                        onClicked: {
                                            settings.outputMode = 0;
                                        }
                                    }
                                    
                                    Button {
                                        id: mixedOutputBtn
                                        width: parent.width / 2
                                        height: parent.height
                                        checkable: true
                                        checked: settings.outputMode === 1
                                        ButtonGroup.group: modeGroup
                                        
                                        background: Rectangle {
                                            radius: Theme.radiusSmall
                                            color: mixedOutputBtn.checked ? Theme.accentColor : "transparent"
                                            
                                            Behavior on color {
                                                ColorAnimation { duration: Theme.animationFast }
                                            }
                                        }
                                        
                                        contentItem: Text {
                                            text: qsTr("混合输出")
                                            color: mixedOutputBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                            font.pixelSize: Theme.fontBody
                                            font.weight: mixedOutputBtn.checked ? Font.DemiBold : Font.Normal
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        
                                        onClicked: {
                                            settings.outputMode = 1;
                                        }
                                    }
                                }
                            }
                        }
                        
                        // Output Parameters Group（灰化按行控制：仅采样率/位深两行在直接输出模式下禁用）
                        ColumnLayout {
                            id: outputParamsGroup
                            objectName: "outputParamsGroup"
                            Layout.fillWidth: true
                            spacing: Theme.spacing16
                            
                            // Row 2: Sample Rate（直接输出模式下灰化）
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: !settings.sampleParamsGreyed
                                opacity: settings.sampleParamsGreyed ? 0.45 : 1.0
                                
                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }
                                
                                Text {
                                    text: qsTr("采样率")
                                    color: settings.sampleParamsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }
                                
                                ComboBox {
                                    id: sampleRateCombo
                                    Layout.preferredWidth: 200
                                    Layout.preferredHeight: 32
                                    
                                    model: settings.sampleRateOptions
                                    textRole: "label"
                                    valueRole: "value"
                                    
                                    Binding {
                                        target: sampleRateCombo
                                        property: "currentIndex"
                                        value: {
                                            for (var i = 0; i < sampleRateCombo.model.length; i++) {
                                                if (sampleRateCombo.model[i].value === settings.sampleRate) {
                                                    return i;
                                                }
                                            }
                                            return 0;
                                        }
                                        restoreMode: Binding.RestoreBindingOrValue
                                    }
                                    
                                    onActivated: function(index) {
                                        settings.sampleRate = model[index].value;
                                    }
                                    
                                    background: Rectangle {
                                        color: sampleRateCombo.hovered ? Theme.hoverColor : Theme.baseColor
                                        radius: Theme.radiusSmall
                                        border.color: Theme.borderColor
                                        border.width: 1
                                    }
                                    
                                    contentItem: Text {
                                        text: sampleRateCombo.displayText
                                        color: settings.sampleParamsGreyed ? Theme.textDisabled : Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: Theme.spacing8
                                        rightPadding: sampleRateCombo.indicator.width + Theme.spacing8
                                        elide: Text.ElideRight
                                    }
                                    
                                    indicator: Text {
                                        text: "▼"
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.spacing8
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    delegate: ItemDelegate {
                                        width: sampleRateCombo.width
                                        height: 32
                                        required property var modelData
                                        required property int index
                                        
                                        contentItem: Text {
                                            text: modelData.label
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: Theme.spacing8
                                        }
                                        
                                        background: Rectangle {
                                            color: parent.hovered ? Theme.hoverColor : Theme.raisedSurfaceColor
                                        }
                                    }
                                    
                                    popup: Popup {
                                        width: sampleRateCombo.width
                                        height: fullListHeight
                                        margins: Theme.spacing8
                                        padding: Theme.spacing4
                                        readonly property real fullListHeight: contentItem.implicitHeight + topPadding + bottomPadding
                                        readonly property real comboTopInWindow: sampleRateCombo.mapToItem(null, 0, 0).y
                                        readonly property real preferredY: sampleRateCombo.height
                                        readonly property real windowTopLimit: margins
                                        readonly property real windowBottomLimit: sampleRateCombo.Window.window ? sampleRateCombo.Window.window.height - margins : comboTopInWindow + preferredY + fullListHeight
                                        readonly property real minY: windowTopLimit - comboTopInWindow
                                        readonly property real maxY: windowBottomLimit - comboTopInWindow - fullListHeight
                                        y: Math.max(minY, Math.min(preferredY, maxY))
                                        
                                        contentItem: ListView {
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: sampleRateCombo.popup.visible ? sampleRateCombo.delegateModel : null
                                            currentIndex: sampleRateCombo.highlightedIndex
                                        }
                                        
                                        background: Rectangle {
                                            color: Theme.raisedSurfaceColor
                                            radius: Theme.radiusSmall
                                            border.color: Theme.borderColor
                                            border.width: 1
                                        }
                                    }
                                }
                            }
                            
                            // Row 2.5: Bit Depth (sampleFormat)（直接输出模式下灰化）
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: !settings.sampleParamsGreyed
                                opacity: settings.sampleParamsGreyed ? 0.45 : 1.0
                                
                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }
                                
                                Text {
                                    text: qsTr("位深")
                                    color: settings.sampleParamsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }
                                
                                ComboBox {
                                    id: sampleFormatCombo
                                    Layout.preferredWidth: 200
                                    Layout.preferredHeight: 32
                                    
                                    model: settings.sampleFormatOptions
                                    textRole: "label"
                                    valueRole: "value"
                                    
                                    Binding {
                                        target: sampleFormatCombo
                                        property: "currentIndex"
                                        value: {
                                            for (var i = 0; i < sampleFormatCombo.model.length; i++) {
                                                if (sampleFormatCombo.model[i].value === settings.sampleFormat) {
                                                    return i;
                                                }
                                            }
                                            return 0;
                                        }
                                        restoreMode: Binding.RestoreBindingOrValue
                                    }
                                    
                                    onActivated: function(index) {
                                        settings.sampleFormat = model[index].value;
                                    }
                                    
                                    background: Rectangle {
                                        color: sampleFormatCombo.hovered ? Theme.hoverColor : Theme.baseColor
                                        radius: Theme.radiusSmall
                                        border.color: Theme.borderColor
                                        border.width: 1
                                    }
                                    
                                    contentItem: Text {
                                        text: sampleFormatCombo.displayText
                                        color: settings.sampleParamsGreyed ? Theme.textDisabled : Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: Theme.spacing8
                                        rightPadding: sampleFormatCombo.indicator.width + Theme.spacing8
                                        elide: Text.ElideRight
                                    }
                                    
                                    indicator: Text {
                                        text: "▼"
                                        color: Theme.secondaryTextColor
                                        font.pixelSize: 10
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.spacing8
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    
                                    delegate: ItemDelegate {
                                        width: sampleFormatCombo.width
                                        height: 32
                                        required property var modelData
                                        required property int index
                                        
                                        contentItem: Text {
                                            text: modelData.label
                                            color: Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: Theme.spacing8
                                        }
                                        
                                        background: Rectangle {
                                            color: parent.hovered ? Theme.hoverColor : Theme.raisedSurfaceColor
                                        }
                                    }
                                    
                                    popup: Popup {
                                        width: sampleFormatCombo.width
                                        height: fullListHeight
                                        margins: Theme.spacing8
                                        padding: Theme.spacing4
                                        readonly property real fullListHeight: contentItem.implicitHeight + topPadding + bottomPadding
                                        readonly property real comboTopInWindow: sampleFormatCombo.mapToItem(null, 0, 0).y
                                        readonly property real preferredY: sampleFormatCombo.height
                                        readonly property real windowTopLimit: margins
                                        readonly property real windowBottomLimit: sampleFormatCombo.Window.window ? sampleFormatCombo.Window.window.height - margins : comboTopInWindow + preferredY + fullListHeight
                                        readonly property real minY: windowTopLimit - comboTopInWindow
                                        readonly property real maxY: windowBottomLimit - comboTopInWindow - fullListHeight
                                        y: Math.max(minY, Math.min(preferredY, maxY))
                                        
                                        contentItem: ListView {
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: sampleFormatCombo.popup.visible ? sampleFormatCombo.delegateModel : null
                                            currentIndex: sampleFormatCombo.highlightedIndex
                                        }
                                        
                                        background: Rectangle {
                                            color: Theme.raisedSurfaceColor
                                            radius: Theme.radiusSmall
                                            border.color: Theme.borderColor
                                            border.width: 1
                                        }
                                    }
                                }
                            }
                            
                            // Row 3: Buffer Duration
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: qsTr("缓冲时长")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }
                                
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing12
                                    
                                    Slider {
                                        id: bufferSlider
                                        Layout.fillWidth: true
                                        from: 50
                                        to: 1000
                                        stepSize: 50
                                        value: settings.bufferDurationMs
                                        onMoved: {
                                            settings.bufferDurationMs = value;
                                        }
                                        
                                        background: Rectangle {
                                            x: bufferSlider.leftPadding
                                            y: bufferSlider.topPadding + bufferSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 4
                                            width: bufferSlider.availableWidth
                                            height: implicitHeight
                                            radius: 2
                                            color: Theme.progressBarTrackColor
                                            
                                            Rectangle {
                                                width: bufferSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: Theme.progressBarColor
                                                radius: 2
                                            }
                                        }
                                        
                                        handle: Rectangle {
                                            x: bufferSlider.leftPadding + bufferSlider.visualPosition * (bufferSlider.availableWidth - width)
                                            y: bufferSlider.topPadding + bufferSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            radius: 8
                                            color: bufferSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (bufferSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                        }
                                    }
                                    
                                    Text {
                                        text: `${settings.bufferDurationMs} ms`
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        Layout.preferredWidth: 50
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }
                    }
                }

                // ==========================================
                // Card 1.5: 播放过渡 (Playback Transition Card)
                // 灰化行集 {1 自动档, 4 预加载, 5 交叉长度, 8 手动档, 9 手动短交叉}
                // 绑定 settings.advanceTransitionsGreyed（Direct 输出=灰化）；{2,3,6,7} 恒可用。
                // ==========================================
                Rectangle {
                    objectName: "transitionCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: transitionCardLayout.implicitHeight + Theme.spacing16 * 2
                    color: Theme.raisedSurfaceColor
                    radius: Theme.radiusMedium
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: transitionCardLayout
                        anchors.fill: parent
                        anchors.margins: Theme.spacing16
                        spacing: Theme.spacing12

                        Text {
                            text: qsTr("播放过渡")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }

                        // 设置 1：自动前进淡入淡出（3 档；仅 Mixed 生效 → Direct 灰化）
                        ColumnLayout {
                            id: autoModeGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: autoModeRow
                                Layout.fillWidth: true
                                enabled: !settings.advanceTransitionsGreyed
                                opacity: settings.advanceTransitionsGreyed ? 0.45 : 1.0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }

                                Text {
                                    text: qsTr("自动前进淡变")
                                    color: settings.advanceTransitionsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                ButtonGroup {
                                    id: autoFadeGroup
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 180
                                    Layout.preferredHeight: 32
                                    color: Theme.surfaceColor
                                    radius: Theme.radiusSmall
                                    border.color: Theme.borderSubtle
                                    border.width: 1

                                    Row {
                                        anchors.fill: parent
                                        spacing: 0

                                        Button {
                                            id: autoFadeNoneBtn
                                            width: parent.width / 3
                                            height: parent.height
                                            checkable: true
                                            checked: settings.autoAdvanceFadeMode === 0
                                            ButtonGroup.group: autoFadeGroup

                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                color: autoFadeNoneBtn.checked ? Theme.accentColor : "transparent"

                                                Behavior on color {
                                                    ColorAnimation { duration: Theme.animationFast }
                                                }
                                            }

                                            contentItem: Text {
                                                text: qsTr("无")
                                                color: autoFadeNoneBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                                font.pixelSize: Theme.fontBody
                                                font.weight: autoFadeNoneBtn.checked ? Font.DemiBold : Font.Normal
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            onClicked: {
                                                settings.autoAdvanceFadeMode = 0;
                                            }
                                        }

                                        Button {
                                            id: autoFadeExceptCueBtn
                                            width: parent.width / 3
                                            height: parent.height
                                            checkable: true
                                            checked: settings.autoAdvanceFadeMode === 1
                                            ButtonGroup.group: autoFadeGroup

                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                color: autoFadeExceptCueBtn.checked ? Theme.accentColor : "transparent"

                                                Behavior on color {
                                                    ColorAnimation { duration: Theme.animationFast }
                                                }
                                            }

                                            contentItem: Text {
                                                text: qsTr("常规交叉")
                                                color: autoFadeExceptCueBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                                font.pixelSize: Theme.fontBody
                                                font.weight: autoFadeExceptCueBtn.checked ? Font.DemiBold : Font.Normal
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            onClicked: {
                                                settings.autoAdvanceFadeMode = 1;
                                            }
                                        }

                                        Button {
                                            id: autoFadeAllBtn
                                            width: parent.width / 3
                                            height: parent.height
                                            checkable: true
                                            checked: settings.autoAdvanceFadeMode === 2
                                            ButtonGroup.group: autoFadeGroup

                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                color: autoFadeAllBtn.checked ? Theme.accentColor : "transparent"

                                                Behavior on color {
                                                    ColorAnimation { duration: Theme.animationFast }
                                                }
                                            }

                                            contentItem: Text {
                                                text: qsTr("全交叉")
                                                color: autoFadeAllBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                                font.pixelSize: Theme.fontBody
                                                font.weight: autoFadeAllBtn.checked ? Font.DemiBold : Font.Normal
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            onClicked: {
                                                settings.autoAdvanceFadeMode = 2;
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                id: autoModeHint
                                Layout.fillWidth: true
                                text: settings.advanceTransitionsGreyed
                                      ? qsTr("仅混合输出可用")
                                      : qsTr("当前曲自然播完自动前进时生效；「常规交叉」对同一 CUE 相邻轨保持无缝（不交叉），「全交叉」对全部邻曲交叉。")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 8：手动改变音轨淡入淡出（3 档；仅 Mixed 生效 → Direct 灰化）
                        ColumnLayout {
                            id: manualModeGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: manualModeRow
                                Layout.fillWidth: true
                                enabled: !settings.advanceTransitionsGreyed
                                opacity: settings.advanceTransitionsGreyed ? 0.45 : 1.0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }

                                Text {
                                    text: qsTr("手动切歌淡变")
                                    color: settings.advanceTransitionsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                ButtonGroup {
                                    id: manualFadeGroup
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 180
                                    Layout.preferredHeight: 32
                                    color: Theme.surfaceColor
                                    radius: Theme.radiusSmall
                                    border.color: Theme.borderSubtle
                                    border.width: 1

                                    Row {
                                        anchors.fill: parent
                                        spacing: 0

                                        Button {
                                            id: manualFadeNoneBtn
                                            width: parent.width / 3
                                            height: parent.height
                                            checkable: true
                                            checked: settings.manualAdvanceFadeMode === 0
                                            ButtonGroup.group: manualFadeGroup

                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                color: manualFadeNoneBtn.checked ? Theme.accentColor : "transparent"

                                                Behavior on color {
                                                    ColorAnimation { duration: Theme.animationFast }
                                                }
                                            }

                                            contentItem: Text {
                                                text: qsTr("无")
                                                color: manualFadeNoneBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                                font.pixelSize: Theme.fontBody
                                                font.weight: manualFadeNoneBtn.checked ? Font.DemiBold : Font.Normal
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            onClicked: {
                                                settings.manualAdvanceFadeMode = 0;
                                            }
                                        }

                                        Button {
                                            id: manualFadeDipBtn
                                            width: parent.width / 3
                                            height: parent.height
                                            checkable: true
                                            checked: settings.manualAdvanceFadeMode === 1
                                            ButtonGroup.group: manualFadeGroup

                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                color: manualFadeDipBtn.checked ? Theme.accentColor : "transparent"

                                                Behavior on color {
                                                    ColorAnimation { duration: Theme.animationFast }
                                                }
                                            }

                                            contentItem: Text {
                                                text: qsTr("短时渐隐")
                                                color: manualFadeDipBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                                font.pixelSize: Theme.fontBody
                                                font.weight: manualFadeDipBtn.checked ? Font.DemiBold : Font.Normal
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            onClicked: {
                                                settings.manualAdvanceFadeMode = 1;
                                            }
                                        }

                                        Button {
                                            id: manualFadeCrossBtn
                                            width: parent.width / 3
                                            height: parent.height
                                            checkable: true
                                            checked: settings.manualAdvanceFadeMode === 2
                                            ButtonGroup.group: manualFadeGroup

                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                color: manualFadeCrossBtn.checked ? Theme.accentColor : "transparent"

                                                Behavior on color {
                                                    ColorAnimation { duration: Theme.animationFast }
                                                }
                                            }

                                            contentItem: Text {
                                                text: qsTr("交叉淡入淡出")
                                                color: manualFadeCrossBtn.checked ? Theme.textOnAccent : Theme.textSecondary
                                                font.pixelSize: Theme.fontBody
                                                font.weight: manualFadeCrossBtn.checked ? Font.DemiBold : Font.Normal
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            onClicked: {
                                                settings.manualAdvanceFadeMode = 2;
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                id: manualModeHint
                                Layout.fillWidth: true
                                text: settings.advanceTransitionsGreyed
                                      ? qsTr("仅混合输出可用")
                                      : qsTr("手动切歌（上一首/下一首）时生效；「短时渐隐」为短暂压音（dip），「交叉淡入淡出」与下方交叉长度联动。")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 2：播放/暂停/停止淡入淡出（全局，恒可用）
                        ColumnLayout {
                            id: transportSwitchGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: transportSwitchRow
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("传送淡入淡出")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                Switch {
                                    id: transportSwitch
                                    Layout.preferredWidth: 48
                                    Layout.preferredHeight: 32
                                    padding: 0
                                    checked: settings.fadeOnTransport
                                    onToggled: {
                                        settings.fadeOnTransport = checked;
                                    }

                                    indicator: Rectangle {
                                        id: transportSwitchTrack
                                        implicitWidth: 36
                                        implicitHeight: 20
                                        radius: 10
                                        x: (transportSwitch.width - width) / 2
                                        y: (transportSwitch.height - height) / 2
                                        color: transportSwitch.checked ? Theme.accentColor
                                                                      : (transportSwitch.hovered ? Theme.hoverColor : Theme.baseColor)
                                        border.color: transportSwitch.checked ? "transparent" : Theme.borderColor
                                        border.width: 1

                                        Behavior on color {
                                            ColorAnimation { duration: Theme.animationFast }
                                        }

                                        Rectangle {
                                            id: transportSwitchKnob
                                            width: 16
                                            height: 16
                                            radius: 8
                                            y: (transportSwitchTrack.height - height) / 2
                                            x: transportSwitch.checked ? transportSwitchTrack.width - width - 2 : 2
                                            color: transportSwitch.checked ? Theme.textOnAccent : Theme.textSecondary

                                            Behavior on x {
                                                NumberAnimation { duration: Theme.animationFast }
                                            }

                                            Behavior on color {
                                                ColorAnimation { duration: Theme.animationFast }
                                            }
                                        }
                                    }

                                    contentItem: Item {
                                    }
                                }
                            }

                            Text {
                                id: transportSwitchHint
                                Layout.fillWidth: true
                                text: qsTr("作用于播放、暂停、停止操作（全局生效，Direct 输出同样可用）")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 3：调整播放进度（seek）淡入淡出（全局，恒可用）
                        ColumnLayout {
                            id: seekSwitchGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: seekSwitchRow
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("进度淡入淡出")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                Switch {
                                    id: seekSwitch
                                    Layout.preferredWidth: 48
                                    Layout.preferredHeight: 32
                                    padding: 0
                                    checked: settings.fadeOnSeek
                                    onToggled: {
                                        settings.fadeOnSeek = checked;
                                    }

                                    indicator: Rectangle {
                                        id: seekSwitchTrack
                                        implicitWidth: 36
                                        implicitHeight: 20
                                        radius: 10
                                        x: (seekSwitch.width - width) / 2
                                        y: (seekSwitch.height - height) / 2
                                        color: seekSwitch.checked ? Theme.accentColor
                                                                  : (seekSwitch.hovered ? Theme.hoverColor : Theme.baseColor)
                                        border.color: seekSwitch.checked ? "transparent" : Theme.borderColor
                                        border.width: 1

                                        Behavior on color {
                                            ColorAnimation { duration: Theme.animationFast }
                                        }

                                        Rectangle {
                                            id: seekSwitchKnob
                                            width: 16
                                            height: 16
                                            radius: 8
                                            y: (seekSwitchTrack.height - height) / 2
                                            x: seekSwitch.checked ? seekSwitchTrack.width - width - 2 : 2
                                            color: seekSwitch.checked ? Theme.textOnAccent : Theme.textSecondary

                                            Behavior on x {
                                                NumberAnimation { duration: Theme.animationFast }
                                            }

                                            Behavior on color {
                                                ColorAnimation { duration: Theme.animationFast }
                                            }
                                        }
                                    }

                                    contentItem: Item {
                                    }
                                }
                            }

                            Text {
                                id: seekSwitchHint
                                Layout.fillWidth: true
                                text: qsTr("作用于拖动进度 / seek（全局生效，Direct 输出同样可用）")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 5：交叉淡入淡出长度（仅 Mixed 生效 → Direct 灰化）
                        ColumnLayout {
                            id: crossfadeGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: crossfadeRow
                                Layout.fillWidth: true
                                enabled: !settings.advanceTransitionsGreyed
                                opacity: settings.advanceTransitionsGreyed ? 0.45 : 1.0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }

                                Text {
                                    text: qsTr("交叉长度")
                                    color: settings.advanceTransitionsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing12

                                    Slider {
                                        id: crossfadeSlider
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 10000
                                        stepSize: settings.transitionSliderStepMs
                                        value: settings.crossfadeMs
                                        onMoved: {
                                            settings.crossfadeMs = value;
                                        }

                                        background: Rectangle {
                                            x: crossfadeSlider.leftPadding
                                            y: crossfadeSlider.topPadding + crossfadeSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 4
                                            width: crossfadeSlider.availableWidth
                                            height: implicitHeight
                                            radius: 2
                                            color: Theme.progressBarTrackColor

                                            Rectangle {
                                                width: crossfadeSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: Theme.progressBarColor
                                                radius: 2
                                            }
                                        }

                                        handle: Rectangle {
                                            x: crossfadeSlider.leftPadding + crossfadeSlider.visualPosition * (crossfadeSlider.availableWidth - width)
                                            y: crossfadeSlider.topPadding + crossfadeSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            radius: 8
                                            color: crossfadeSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (crossfadeSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                        }
                                    }

                                    Text {
                                        text: `${settings.crossfadeMs} ms`
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Text {
                                id: crossfadeHint
                                Layout.fillWidth: true
                                text: settings.advanceTransitionsGreyed
                                      ? qsTr("仅混合输出可用")
                                      : qsTr("自动前进「全交叉」档与手动切歌「交叉淡入淡出」档共用的长度")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 6：播放/暂停/停止淡变长度（全局，恒可用）
                        ColumnLayout {
                            id: transportFadeGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: transportFadeRow
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("传送淡变长度")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing12

                                    Slider {
                                        id: transportFadeSlider
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 3000
                                        stepSize: settings.transitionSliderStepMs
                                        value: settings.transportFadeMs
                                        onMoved: {
                                            settings.transportFadeMs = value;
                                        }

                                        background: Rectangle {
                                            x: transportFadeSlider.leftPadding
                                            y: transportFadeSlider.topPadding + transportFadeSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 4
                                            width: transportFadeSlider.availableWidth
                                            height: implicitHeight
                                            radius: 2
                                            color: Theme.progressBarTrackColor

                                            Rectangle {
                                                width: transportFadeSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: Theme.progressBarColor
                                                radius: 2
                                            }
                                        }

                                        handle: Rectangle {
                                            x: transportFadeSlider.leftPadding + transportFadeSlider.visualPosition * (transportFadeSlider.availableWidth - width)
                                            y: transportFadeSlider.topPadding + transportFadeSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            radius: 8
                                            color: transportFadeSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (transportFadeSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                        }
                                    }

                                    Text {
                                        text: `${settings.transportFadeMs} ms`
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Text {
                                id: transportFadeHint
                                Layout.fillWidth: true
                                text: qsTr("播放 / 暂停 / 停止操作淡变时长（全局，含 Direct 输出）")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 7：seek 淡变长度（全局，恒可用）
                        ColumnLayout {
                            id: seekFadeGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: seekFadeRow
                                Layout.fillWidth: true

                                Text {
                                    text: qsTr("进度淡变长度")
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing12

                                    Slider {
                                        id: seekFadeSlider
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 3000
                                        stepSize: settings.transitionSliderStepMs
                                        value: settings.seekFadeMs
                                        onMoved: {
                                            settings.seekFadeMs = value;
                                        }

                                        background: Rectangle {
                                            x: seekFadeSlider.leftPadding
                                            y: seekFadeSlider.topPadding + seekFadeSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 4
                                            width: seekFadeSlider.availableWidth
                                            height: implicitHeight
                                            radius: 2
                                            color: Theme.progressBarTrackColor

                                            Rectangle {
                                                width: seekFadeSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: Theme.progressBarColor
                                                radius: 2
                                            }
                                        }

                                        handle: Rectangle {
                                            x: seekFadeSlider.leftPadding + seekFadeSlider.visualPosition * (seekFadeSlider.availableWidth - width)
                                            y: seekFadeSlider.topPadding + seekFadeSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            radius: 8
                                            color: seekFadeSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (seekFadeSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                        }
                                    }

                                    Text {
                                        text: `${settings.seekFadeMs} ms`
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Text {
                                id: seekFadeHint
                                Layout.fillWidth: true
                                text: qsTr("拖动进度（seek）操作淡变时长（全局，含 Direct 输出）")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 4：预先加载无间隙音轨（预解码提前量；仅 Mixed 生效 → Direct 灰化）
                        ColumnLayout {
                            id: preloadGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: preloadRow
                                Layout.fillWidth: true
                                enabled: !settings.advanceTransitionsGreyed
                                opacity: settings.advanceTransitionsGreyed ? 0.45 : 1.0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }

                                Text {
                                    text: qsTr("预加载")
                                    color: settings.advanceTransitionsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing12

                                    Slider {
                                        id: preloadSlider
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 5000
                                        stepSize: settings.transitionSliderStepMs
                                        value: settings.gaplessPreloadMs
                                        onMoved: {
                                            settings.gaplessPreloadMs = value;
                                        }

                                        background: Rectangle {
                                            x: preloadSlider.leftPadding
                                            y: preloadSlider.topPadding + preloadSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 4
                                            width: preloadSlider.availableWidth
                                            height: implicitHeight
                                            radius: 2
                                            color: Theme.progressBarTrackColor

                                            Rectangle {
                                                width: preloadSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: Theme.progressBarColor
                                                radius: 2
                                            }
                                        }

                                        handle: Rectangle {
                                            x: preloadSlider.leftPadding + preloadSlider.visualPosition * (preloadSlider.availableWidth - width)
                                            y: preloadSlider.topPadding + preloadSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            radius: 8
                                            color: preloadSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (preloadSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                        }
                                    }

                                    Text {
                                        text: `${settings.gaplessPreloadMs} ms`
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Text {
                                id: preloadHint
                                Layout.fillWidth: true
                                text: settings.advanceTransitionsGreyed
                                      ? qsTr("仅混合输出可用")
                                      : qsTr("无间隙音轨预先解码的触发提前量")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        // 设置 9：手动短交叉长度（仅 Mixed 生效 → Direct 灰化）
                        ColumnLayout {
                            id: manualShortGroup
                            Layout.fillWidth: true
                            spacing: Theme.spacing2

                            RowLayout {
                                id: manualShortRow
                                Layout.fillWidth: true
                                enabled: !settings.advanceTransitionsGreyed
                                opacity: settings.advanceTransitionsGreyed ? 0.45 : 1.0

                                Behavior on opacity {
                                    NumberAnimation { duration: Theme.animationFast }
                                }

                                Text {
                                    text: qsTr("手动短交叉长度")
                                    color: settings.advanceTransitionsGreyed ? Theme.textDisabled : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 100
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing12

                                    Slider {
                                        id: manualShortSlider
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 3000
                                        stepSize: settings.transitionSliderStepMs
                                        value: settings.manualShortCrossfadeMs
                                        onMoved: {
                                            settings.manualShortCrossfadeMs = value;
                                        }

                                        background: Rectangle {
                                            x: manualShortSlider.leftPadding
                                            y: manualShortSlider.topPadding + manualShortSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 200
                                            implicitHeight: 4
                                            width: manualShortSlider.availableWidth
                                            height: implicitHeight
                                            radius: 2
                                            color: Theme.progressBarTrackColor

                                            Rectangle {
                                                width: manualShortSlider.visualPosition * parent.width
                                                height: parent.height
                                                color: Theme.progressBarColor
                                                radius: 2
                                            }
                                        }

                                        handle: Rectangle {
                                            x: manualShortSlider.leftPadding + manualShortSlider.visualPosition * (manualShortSlider.availableWidth - width)
                                            y: manualShortSlider.topPadding + manualShortSlider.availableHeight / 2 - height / 2
                                            implicitWidth: 16
                                            implicitHeight: 16
                                            radius: 8
                                            color: manualShortSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (manualShortSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                        }
                                    }

                                    Text {
                                        text: `${settings.manualShortCrossfadeMs} ms`
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Text {
                                id: manualShortHint
                                Layout.fillWidth: true
                                text: settings.advanceTransitionsGreyed
                                      ? qsTr("仅混合输出可用")
                                      : qsTr("手动切歌「短时渐隐」档的淡变长度")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                // ==========================================
                // Card 2: 设备与调试配置 (Device & Logging Card)
                // ==========================================
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: deviceCardLayout.implicitHeight + Theme.spacing16 * 2
                    color: Theme.raisedSurfaceColor
                    radius: Theme.radiusMedium
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: deviceCardLayout
                        anchors.fill: parent
                        anchors.margins: Theme.spacing16
                        spacing: Theme.spacing16

                        Text {
                            text: qsTr("设备与系统")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                        }

                        // Row 4: Output Device
                        RowLayout {
                            Layout.fillWidth: true
                            opacity: deviceCombo.enabled ? 1.0 : 0.5
                            
                            Text {
                                text: qsTr("输出设备")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                                Layout.preferredWidth: 100
                            }
                            
                            ComboBox {
                                id: deviceCombo
                                Layout.fillWidth: true
                                Layout.minimumWidth: 120
                                Layout.preferredHeight: 32
                                
                                model: settings.playbackDeviceNames
                                enabled: settings.playbackDevices.length > 0
                                
                                displayText: enabled ? currentText : qsTr("无可用输出设备")
                                
                                Binding {
                                    target: deviceCombo
                                    property: "currentIndex"
                                    value: {
                                        if (!deviceCombo.enabled) return -1;
                                        var idx = settings.playbackDevices.indexOf(settings.preferredDeviceId);
                                        return idx >= 0 ? idx : 0;
                                    }
                                    restoreMode: Binding.RestoreBindingOrValue
                                }
                                
                                onActivated: function(index) {
                                    if (index >= 0 && index < settings.playbackDevices.length) {
                                        settings.preferredDeviceId = settings.playbackDevices[index];
                                    }
                                }
                                
                                background: Rectangle {
                                    color: !deviceCombo.enabled ? Theme.borderSubtle : (deviceCombo.hovered ? Theme.hoverColor : Theme.baseColor)
                                    radius: Theme.radiusSmall
                                    border.color: Theme.borderColor
                                    border.width: 1
                                }
                                
                                contentItem: Text {
                                    text: deviceCombo.displayText
                                    color: deviceCombo.enabled ? Theme.textPrimary : Theme.textDisabled
                                    font.pixelSize: Theme.fontBody
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: Theme.spacing8
                                    rightPadding: deviceCombo.indicator.width + Theme.spacing8
                                    elide: Text.ElideRight
                                }
                                
                                indicator: Text {
                                    text: "▼"
                                    color: Theme.secondaryTextColor
                                    font.pixelSize: 10
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spacing8
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: deviceCombo.enabled
                                }
                                
                                delegate: ItemDelegate {
                                    width: deviceCombo.width
                                    height: 32
                                    required property var modelData
                                    required property int index
                                    
                                    contentItem: Text {
                                        text: modelData
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: Theme.spacing8
                                        rightPadding: Theme.spacing8
                                        elide: Text.ElideRight
                                    }
                                    
                                    background: Rectangle {
                                        color: parent.hovered ? Theme.hoverColor : Theme.raisedSurfaceColor
                                    }
                                }
                                
                                popup: Popup {
                                    width: deviceCombo.width
                                    height: fullListHeight
                                    margins: Theme.spacing8
                                    padding: Theme.spacing4
                                    readonly property real fullListHeight: contentItem.implicitHeight + topPadding + bottomPadding
                                    readonly property real comboTopInWindow: deviceCombo.mapToItem(null, 0, 0).y
                                    readonly property real preferredY: deviceCombo.height
                                    readonly property real windowTopLimit: margins
                                    readonly property real windowBottomLimit: deviceCombo.Window.window ? deviceCombo.Window.window.height - margins : comboTopInWindow + preferredY + fullListHeight
                                    readonly property real minY: windowTopLimit - comboTopInWindow
                                    readonly property real maxY: windowBottomLimit - comboTopInWindow - fullListHeight
                                    y: Math.max(minY, Math.min(preferredY, maxY))
                                    
                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: deviceCombo.popup.visible ? deviceCombo.delegateModel : null
                                        currentIndex: deviceCombo.highlightedIndex
                                    }
                                    
                                    background: Rectangle {
                                        color: Theme.raisedSurfaceColor
                                        radius: Theme.radiusSmall
                                        border.color: Theme.borderColor
                                        border.width: 1
                                    }
                                }
                            }
                        }
                        
                        // Row 5: Log Level
                        RowLayout {
                            Layout.fillWidth: true
                            
                            Text {
                                text: qsTr("日志等级")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                                Layout.preferredWidth: 100
                            }
                            
                            ComboBox {
                                id: logLevelCombo
                                Layout.preferredWidth: 200
                                Layout.preferredHeight: 32
                                
                                model: [
                                    { value: 0, label: qsTr("Trace") },
                                    { value: 1, label: qsTr("Debug") },
                                    { value: 2, label: qsTr("Info") },
                                    { value: 3, label: qsTr("Warn") },
                                    { value: 4, label: qsTr("Error") },
                                    { value: 5, label: qsTr("Critical") }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                
                                Binding {
                                    target: logLevelCombo
                                    property: "currentIndex"
                                    value: {
                                        for (var i = 0; i < logLevelCombo.model.length; i++) {
                                            if (logLevelCombo.model[i].value === settings.logLevel) {
                                                return i;
                                            }
                                        }
                                        return 0;
                                    }
                                    restoreMode: Binding.RestoreBindingOrValue
                                }
                                
                                onActivated: function(index) {
                                    settings.logLevel = model[index].value;
                                }
                                
                                background: Rectangle {
                                    color: logLevelCombo.hovered ? Theme.hoverColor : Theme.baseColor
                                    radius: Theme.radiusSmall
                                    border.color: Theme.borderColor
                                    border.width: 1
                                }
                                
                                contentItem: Text {
                                    text: logLevelCombo.displayText
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: Theme.spacing8
                                    rightPadding: logLevelCombo.indicator.width + Theme.spacing8
                                    elide: Text.ElideRight
                                }
                                
                                indicator: Text {
                                    text: "▼"
                                    color: Theme.secondaryTextColor
                                    font.pixelSize: 10
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.spacing8
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                
                                delegate: ItemDelegate {
                                    width: logLevelCombo.width
                                    height: 32
                                    required property var modelData
                                    required property int index
                                    
                                    contentItem: Text {
                                        text: modelData.label
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        verticalAlignment: Text.AlignVCenter
                                        leftPadding: Theme.spacing8
                                    }
                                    
                                    background: Rectangle {
                                        color: parent.hovered ? Theme.hoverColor : Theme.raisedSurfaceColor
                                    }
                                }
                                
                                popup: Popup {
                                    width: logLevelCombo.width
                                    height: fullListHeight
                                    margins: Theme.spacing8
                                    padding: Theme.spacing4
                                    readonly property real fullListHeight: contentItem.implicitHeight + topPadding + bottomPadding
                                    readonly property real comboTopInWindow: logLevelCombo.mapToItem(null, 0, 0).y
                                    readonly property real preferredY: logLevelCombo.height
                                    readonly property real windowTopLimit: margins
                                    readonly property real windowBottomLimit: logLevelCombo.Window.window ? logLevelCombo.Window.window.height - margins : comboTopInWindow + preferredY + fullListHeight
                                    readonly property real minY: windowTopLimit - comboTopInWindow
                                    readonly property real maxY: windowBottomLimit - comboTopInWindow - fullListHeight
                                    y: Math.max(minY, Math.min(preferredY, maxY))
                                    
                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: logLevelCombo.popup.visible ? logLevelCombo.delegateModel : null
                                        currentIndex: logLevelCombo.highlightedIndex
                                    }
                                    
                                    background: Rectangle {
                                        color: Theme.raisedSurfaceColor
                                        radius: Theme.radiusSmall
                                        border.color: Theme.borderColor
                                        border.width: 1
                                    }
                                }
                            }
                        }
                    }
                }

                // ==========================================
                // Card 3: 歌词分隔符配置 (Lyric Delimiters Card)
                // ==========================================
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: delimiterCardLayout.implicitHeight + Theme.spacing16 * 2
                    color: Theme.raisedSurfaceColor
                    radius: Theme.radiusMedium
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: delimiterCardLayout
                        anchors.fill: parent
                        anchors.margins: Theme.spacing16
                        spacing: Theme.spacing12

                        // Row 6: Lyric Delimiters Group
                        ColumnLayout {
                            id: delimiterListGroup
                            objectName: "delimiterList"
                            Layout.fillWidth: true
                            spacing: Theme.spacing8
                            
                            Text {
                                text: qsTr("歌词分隔符")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontTitle
                                font.weight: Font.DemiBold
                                Layout.bottomMargin: Theme.spacing4
                            }
                            
                            Repeater {
                                id: delimiterRepeater
                                model: settings.lyricDelimiters
                                
                                delegate: RowLayout {
                                    required property int index
                                    required property string modelData
                                    
                                    Layout.fillWidth: true
                                    spacing: Theme.spacing8
                                    
                                    TextField {
                                        id: delimiterInput
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 32
                                        text: modelData
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontBody
                                        
                                        background: Rectangle {
                                            color: Theme.baseColor
                                            radius: Theme.radiusSmall
                                            border.color: delimiterInput.activeFocus ? Theme.borderAccent : Theme.borderColor
                                            border.width: 1
                                        }
                                        
                                        onEditingFinished: {
                                            if (text === "") {
                                                var list = [];
                                                for (var i = 0; i < settings.lyricDelimiters.length; i++) {
                                                    if (i !== index) {
                                                        list.push(settings.lyricDelimiters[i]);
                                                    }
                                                }
                                                settings.lyricDelimiters = list;
                                            } else {
                                                var list = [];
                                                for (var i = 0; i < settings.lyricDelimiters.length; i++) {
                                                    if (i !== index) {
                                                        list.push(settings.lyricDelimiters[i]);
                                                    }
                                                }
                                                list[index] = text;
                                                settings.lyricDelimiters = list;
                                            }
                                        }
                                    }
                                    
                                    StyleButton {
                                        iconSource: "qrc:/qt/qml/Seriona/qml/assets/close.svg"
                                        Layout.preferredWidth: 32
                                        Layout.preferredHeight: 32
                                        iconSize: 12
                                        textColor: Theme.textSecondary
                                        onClicked: {
                                            var list = [];
                                            for (var i = 0; i < settings.lyricDelimiters.length; i++) {
                                                if (i !== index) {
                                                    list.push(settings.lyricDelimiters[i]);
                                                }
                                            }
                                            settings.lyricDelimiters = list;
                                        }
                                    }
                                }
                            }
                            
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacing8
                                
                                TextField {
                                    id: newDelimiterInput
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    placeholderText: qsTr("输入新分隔符...")
                                    placeholderTextColor: Theme.textDisabled
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    
                                    background: Rectangle {
                                        id: newDelimiterBg
                                        color: Theme.baseColor
                                        radius: Theme.radiusSmall
                                        border.color: Theme.borderColor
                                        border.width: 1
                                        
                                        states: [
                                            State {
                                                name: "error"
                                                PropertyChanges {
                                                    newDelimiterBg.border.color: Theme.dangerColor
                                                }
                                            }
                                        ]
                                        
                                        transitions: [
                                            Transition {
                                                from: ""
                                                to: "error"
                                                ColorAnimation { duration: Theme.animationFast }
                                            }
                                        ]
                                    }
                                    
                                    Timer {
                                        id: errorTimer
                                        interval: 1000
                                        onTriggered: {
                                            newDelimiterBg.state = "";
                                        }
                                    }
                                }
                                
                                Button {
                                    id: addBtn
                                    Layout.preferredHeight: 32
                                    Layout.preferredWidth: 60
                                    
                                    background: Rectangle {
                                        color: addBtn.pressed ? Theme.pressedColor : (addBtn.hovered ? Theme.hoverColor : Theme.baseColor)
                                        radius: Theme.radiusSmall
                                        border.color: Theme.borderColor
                                        border.width: 1
                                    }
                                    
                                    contentItem: Text {
                                        text: qsTr("添加")
                                        color: Theme.accentColor
                                        font.pixelSize: Theme.fontBody
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    
                                    onClicked: {
                                        if (newDelimiterInput.text === "") {
                                            newDelimiterBg.state = "error";
                                            errorTimer.restart();
                                            return;
                                        }
                                        
                                        var list = [];
                                        for (var i = 0; i < settings.lyricDelimiters.length; i++) {
                                            list.push(settings.lyricDelimiters[i]);
                                        }
                                        list.push(newDelimiterInput.text);
                                        settings.lyricDelimiters = list;
                                        newDelimiterInput.text = "";
                                    }
                                }
                            }
                        }

                        // Row: Lyric Follow Restore Delay
                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: qsTr("跟随恢复延迟")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                                Layout.preferredWidth: 100
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacing12

                                Slider {
                                    id: followRestoreSlider
                                    Layout.fillWidth: true
                                    from: 1
                                    to: 15
                                    stepSize: 1
                                    value: settings.followRestoreDelayMs / 1000
                                    onMoved: {
                                        settings.followRestoreDelayMs = value * 1000;
                                    }

                                    background: Rectangle {
                                        x: followRestoreSlider.leftPadding
                                        y: followRestoreSlider.topPadding + followRestoreSlider.availableHeight / 2 - height / 2
                                        implicitWidth: 200
                                        implicitHeight: 4
                                        width: followRestoreSlider.availableWidth
                                        height: implicitHeight
                                        radius: 2
                                        color: Theme.progressBarTrackColor

                                        Rectangle {
                                            width: followRestoreSlider.visualPosition * parent.width
                                            height: parent.height
                                            color: Theme.progressBarColor
                                            radius: 2
                                        }
                                    }

                                    handle: Rectangle {
                                        x: followRestoreSlider.leftPadding + followRestoreSlider.visualPosition * (followRestoreSlider.availableWidth - width)
                                        y: followRestoreSlider.topPadding + followRestoreSlider.availableHeight / 2 - height / 2
                                        implicitWidth: 16
                                        implicitHeight: 16
                                        radius: 8
                                        color: followRestoreSlider.pressed ? Qt.darker(Theme.accentColor, 1.2) : (followRestoreSlider.hovered ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)
                                    }
                                }

                                Text {
                                    text: `${settings.followRestoreDelayMs / 1000} s`
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    Layout.preferredWidth: 50
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    onVisibleChanged: {
        if (visible) {
            settings.enumerateDevices();
            root.requestActivate();
            contentRect.forceActiveFocus();
        }
    }
}
