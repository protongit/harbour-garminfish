import QtQuick 2.0
import QtQuick.Layouts 1.1
import uk.co.protongit.garminfish 1.0
import "../components/"
import "../components/platform"

PageListPL {
    id: page
    title: qsTr("Watch Data")

    property var summary: ({})
    property var allFiles: []
    property string filterType: ""

    pageMenu: PageMenuPL {
        PageMenuItemPL {
            iconSource: styler.iconRefresh !== undefined ? styler.iconRefresh : ""
            text: qsTr("Refresh")
            onClicked: refresh()
        }
    }

    headerExtra: Component {
        Column {
            width: parent ? parent.width : page.width
            spacing: styler.themePaddingSmall

            SectionHeaderPL {
                text: qsTr("Summary")
            }

            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("FIT files: %1 (%2)").arg(summary.fitFiles || 0).arg(summary.fitSize || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Monitor: %1   Sleep: %2   Activity: %3")
                      .arg(summary.monitorFiles || 0)
                      .arg(summary.sleepFiles || 0)
                      .arg(summary.activityFiles || 0)
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Monitoring samples: %1").arg(summary.monitorSamples || 0)
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Sleep stages: %1 in %2 nights (latest %3)")
                      .arg(summary.sleepStages || 0)
                      .arg(summary.sleepNights || 0)
                      .arg(summary.latestSleep || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Activities: %1 (last: %2, load %3)")
                      .arg(summary.activities || 0)
                      .arg(summary.lastActivityName || "-")
                      .arg(summary.lastActivityLoad || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Steps today: %1").arg(summary.stepsToday || 0)
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Last heart rate: %1 bpm (%2)")
                      .arg(summary.lastHeartRate || "-")
                      .arg(summary.lastHeartRateTime || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Battery: %1 %").arg(summary.battery || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Body energy: %1").arg(summary.bodyEnergy || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Last sample: %1").arg(summary.lastSample || "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                color: styler.themeSecondaryHighlightColor
                text: qsTr("Database: %1").arg(summary.databaseSize || "-")
            }

            SectionHeaderPL {
                text: qsTr("Downloaded files")
            }

            ComboBoxPL {
                id: cboFilter
                width: parent.width
                label: qsTr("Filter")
                model: ListModel {
                    ListElement { itemText: qsTr("All") }
                    ListElement { itemText: qsTr("Activity") }
                    ListElement { itemText: qsTr("Monitor") }
                    ListElement { itemText: qsTr("Sleep") }
                    ListElement { itemText: qsTr("Metrics") }
                    ListElement { itemText: qsTr("HRV") }
                }
                onCurrentIndexChanged: {
                    var types = ["", "ACTIVITY", "MONITOR", "SLEEP", "METRICS", "HRV_STATUS"];
                    page.filterType = types[currentIndex] || "";
                    page.applyFilter();
                }
            }
        }
    }

    model: filesModel

    delegate: ListItemPL {
        contentHeight: fileColumn.height + (styler.themePaddingMedium * 2)

        onClicked: {
            app.pages.push(Qt.resolvedUrl("FitFilePage.qml"), {
                "filePath": model.path,
                "fileName": model.name
            });
        }

        Column {
            id: fileColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: styler.themePaddingLarge
            anchors.rightMargin: styler.themePaddingLarge
            spacing: styler.themePaddingSmall

            LabelPL {
                width: parent.width
                text: model.name
                truncMode: truncModes.elide
            }
            LabelPL {
                width: parent.width
                color: styler.themeSecondaryHighlightColor
                font.pixelSize: styler.themeFontSizeSmall
                text: model.type + "  ·  " + model.modified + "  ·  " + model.sizeText
            }
        }
    }

    ListModel {
        id: filesModel
    }

    function applyFilter() {
        filesModel.clear();
        for (var i = 0; i < allFiles.length; i++) {
            var file = allFiles[i];
            if (filterType.length === 0 || file.type === filterType) {
                filesModel.append(file);
            }
        }
    }

    function refresh() {
        summary = dataSource.watchSummary();
        allFiles = dataSource.watchFiles();
        applyFilter();
    }

    Component.onCompleted: refresh()
}
