import "../components/"
import "../components/platform"
import "../components/Translation.js" as T
import QtQuick 2.0
import QtQuick.Layouts 1.1
import uk.co.piggz.amazfish 1.0

PageListPL {
    id: page

    function fncCovertSecondsToString(sec) {
        var iHours = Math.floor(sec / 3600);
        var iMinutes = Math.floor((sec - iHours * 3600) / 60);
        var iSeconds = Math.floor(sec - (iHours * 3600) - (iMinutes * 60));
        return (iHours > 0 ? iHours + "h " : "") + (iMinutes > 0 ? iMinutes + "m " : "") + iSeconds + "s";
    }


    title: qsTr("Sports Activities")
    model: SportsModel
    property string syncStatus: ""
    property bool syncing: DaemonInterfaceInstance.operationRunning
    property var trainingGraph: null

    function refreshActivities() {
        refreshTimer.restart();
    }

    Timer {
        id: refreshTimer
        interval: 500
        repeat: false
        onTriggered: {
            SportsModel.update();
            if (page.trainingGraph) {
                page.trainingGraph.updateGraph(new Date());
            }
        }
    }

    onPageStatusActive: refreshActivities()

    Component.onCompleted: {
        refreshActivities();
    }

    headerExtra: Component {
        Item {
            width: parent ? parent.width : page.width
            implicitHeight: headerColumn.height + styler.themePaddingMedium
            height: implicitHeight

            Column {
                id: headerColumn
                width: parent.width
                spacing: styler.themePaddingSmall

                LabelPL {
                    width: parent.width
                    font.pixelSize: styler.themeFontSizeLarge
                    text: qsTr("Training Load")
                }

                LabelPL {
                    width: parent.width
                    visible: !graphTrainingLoad.noData
                    color: styler.themeSecondaryHighlightColor
                    text: qsTr("Today: %1").arg(graphTrainingLoad.lastValue.toFixed(1))
                }

                Graph {
                    id: graphTrainingLoad
                    graphTitle: qsTr("Last 10 days")
                    graphHeight: 220
                    axisX.mask: "MM/dd"
                    axisY.units: qsTr("Load")
                    type: DataSource.TrainingLoadSummary
                    graphType: bar
                    minY: 0
                    maxY: Math.max(10, suggestedMaxY)
                    valueConverter: function(value) {
                        return value.toFixed(1);
                    }
                    Component.onCompleted: {
                        page.trainingGraph = graphTrainingLoad;
                        updateGraph(new Date());
                    }
                    Component.onDestruction: {
                        if (page.trainingGraph === graphTrainingLoad) {
                            page.trainingGraph = null;
                        }
                    }
                }

                LabelPL {
                    width: parent.width
                    visible: graphTrainingLoad.noData
                    color: styler.themeSecondaryHighlightColor
                    text: qsTr("No training load data yet. Sync an activity to see it here.")
                }

                Row {
                    width: parent.width
                    spacing: styler.themePaddingSmall

                    BusyIndicatorSmallPL {
                        running: page.syncing
                        visible: page.syncing
                        width: styler.themeIconSizeMedium
                        height: width
                    }

                    LabelPL {
                        width: parent.width - (page.syncing ? styler.themeIconSizeMedium : 0)
                        wrapMode: Text.Wrap
                        color: styler.themeSecondaryHighlightColor
                        text: page.syncStatus.length > 0
                              ? page.syncStatus
                              : qsTr("Last sync: not run")
                    }
                }
            }
        }
    }

    IconPL {
        id: sharedIconTime

        iconName: styler.iconClock
        visible: false
    }

    Connections {
        target: DaemonInterfaceInstance
        onMessage: {
            page.syncStatus = text;
        }
        onOperationRunningChanged: {
            if (!DaemonInterfaceInstance.operationRunning) {
                page.refreshActivities();
            }
        }
    }

    pageMenu: PageMenuPL {
        PageMenuItemPL {
            iconSource: styler.iconRefresh !== undefined ? styler.iconRefresh : ""
            text: qsTr("Search activities on watch")
            onClicked: {
                page.syncStatus = qsTr("Searching for activities...");
                DaemonInterfaceInstance.fetchData(Amazfish.TYPE_ACTIVITY);
            }
            enabled: DaemonInterfaceInstance.connectionState === "authenticated" && !page.syncing
        }

    }

    delegate: ListItemPL {
        id: listItem

        contentHeight: styler.themeItemSizeSmall + (styler.themePaddingMedium * 2)
        onClicked: {
            var sportpage = app.pages.push(Qt.resolvedUrl("SportPage.qml"), {
                "activityId": model.id,
                "activitytitle": T.translateSportKind(kindstring) + " - " + Qt.formatDateTime(startdate, "yyyy/MM/dd"),
                "date": Qt.formatDateTime(startdate, "yyyy/MM/dd"),
                "location": [baselatitude, baselongitude, basealtitude],
                "starttime": Qt.formatDateTime(startdate, "hh:mm:ss"),
                "duration": durationLabel.text,
                "times": timesLabel.text,
                "kindstring": kindstring,
                "tcx": SportsModel.gpx(id),
                "rawGpx": SportsModel.rawGpx(id)
            });
            SportsMeta.update(id);
            sportpage.update();
        }

        Row {
            id: listItemRow

            width: parent.width - (2 * styler.themePaddingLarge)
            spacing: styler.themePaddingLarge * 2
            anchors.left: parent.left
            anchors.leftMargin: styler.themePaddingLarge * 2
            anchors.right: parent.right
            anchors.rightMargin: styler.themePaddingLarge * 2

            Loader {
                id: workoutImage

                anchors.top: parent.top
                anchors.topMargin: styler.themePaddingMedium
                width: styler.themeItemSizeSmall
                height: width

                sourceComponent: IconPL {
                    iconSource: styler.activityIconPrefix + "icon-m-" + kindstring.toLowerCase() + styler.customIconSuffix
                    width: styler.themeItemSizeSmall
                    height: width
                }

            }

            Column {
                id: leftColumn

                anchors.top: parent.top
                anchors.topMargin: styler.themePaddingMedium
                width: (parent.width - workoutImage.width - (styler.themePaddingLarge * 4)) * 0.5
                spacing: styler.themePaddingSmall

                LabelPL {
                    id: nameLabel

                    text: T.translateSportKind(kindstring)
                }

                LabelPL {
                    id: dateLabel

                    text: Qt.formatDate(startdate, "ddd") + " " + startdate.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                }

                LabelPL {
                    width: parent.width
                    visible: model.load > 0
                    color: styler.themeSecondaryHighlightColor
                    font.pixelSize: styler.themeFontSizeSmall
                    text: qsTr("Load %1").arg(model.load.toFixed(1))
                }

            }

            Column {
                id: rightColumn

                anchors.top: parent.top
                anchors.topMargin: styler.themePaddingMedium
                width: leftColumn.width
                spacing: styler.themePaddingSmall

                Row {
                    anchors.right: parent.right
                    spacing: styler.themePaddingSmall

                    IconPL {
                        id: durationImage

                        height: durationLabel.height
                        width: height
                        asynchronous: true
                        source: sharedIconTime.source
                    }

                    LabelPL {
                        id: durationLabel

                        text: fncCovertSecondsToString((enddate - startdate) / 1000)
                        horizontalAlignment: Text.AlignRight
                    }

                }

                LabelPL {
                    id: timesLabel

                    anchors.right: parent.right
                    text: startdate.toLocaleTimeString(Qt.locale(), Locale.ShortFormat) + " ⟶ " + enddate.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                    horizontalAlignment: Text.AlignRight
                }

            }

        }

        menu: ContextMenuPL {
            id: contextMenu

            ContextMenuItemPL {
                iconName: styler.iconDelete
                text: qsTr("Remove")
                onClicked: {
                    SportsModel.deleteRecord(id);
                }
            }

        }

    }

}
