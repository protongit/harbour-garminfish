import QtQuick 2.0
import uk.co.protongit.garminfish 1.0
import QtQuick.Layouts 1.1
import "../components/"
import "../components/platform"

PagePL {
    id: page
    title: qsTr("Blood Oxygen")

    property alias day: nav.day
    property var latest: ({})

    pageMenu: PageMenuPL {
        PageMenuItemPL {
            iconSource: styler.iconDownloadData !== undefined ? styler.iconDownloadData : ""
            text: qsTr("Download SPO2")
            onClicked: DaemonInterfaceInstance.fetchData(Amazfish.TYPE_SPO2);
        }
    }

    Column {
        id: column
        width: parent.width
        anchors.top: parent.top
        anchors.margins: styler.themePaddingMedium
        spacing: styler.themePaddingLarge

        LabelPL {
            id: lblSPO2Today
            font.pixelSize: styler.themeFontSizeExtraLarge * 3
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            text: _InfoSpo2 > 0
                  ? qsTr("%1%").arg(_InfoSpo2)
                  : (latest.value !== undefined ? qsTr("%1%").arg(latest.value) : qsTr("No SpO2 data"))
            horizontalAlignment: Text.AlignHCenter
        }

        LabelPL {
            width: parent.width
            visible: latest.time !== undefined
            color: styler.themeSecondaryHighlightColor
            font.pixelSize: styler.themeFontSizeSmall
            text: qsTr("Last synced: %1").arg(latest.time || "-")
            horizontalAlignment: Text.AlignHCenter
        }
        DateNavigation {
            id: nav
            onBackward: {
                var d = new Date(day);
                d.setDate(day.getDate() - 1);
                day = d;
            }
            onForward: {
                var d = new Date(day);
                d.setDate(day.getDate() + 1);
                day = d;
            }
            onDayChanged: {
                updateGraphs();
            }
        }

        Graph {
            id: graphSpo2Normal
            graphTitle: qsTr("Normal SPO2")
            graphHeight: 300

            axisX.mask: "MM/dd"
            axisY.units: qsTr("%")
            type: DataSource.Spo2Normal
            graphType: bar

            minY: 80
            maxY: 100

            valueConverter: function(value) {
                return value.toFixed(0);
            }
            onClicked: {
                updateGraph(day);
            }
        }

        Graph {
            id: graphSpo2Sleep
            graphTitle: qsTr("Sleep SPO2")
            graphHeight: 300
            visible: !noData

            axisX.mask: "MM/dd"
            axisY.units: qsTr("%")
            type: DataSource.Spo2Sleep
            graphType: bar

            minY: 80
            maxY: 100

            valueConverter: function(value) {
                return value.toFixed(0);
            }
            onClicked: {
                updateGraph(day);
            }
        }

        LabelPL {
            width: parent.width
            visible: graphSpo2Sleep.noData
            wrapMode: Text.Wrap
            color: styler.themeSecondaryHighlightColor
            text: qsTr("No sleep SpO2 data from this watch")
            horizontalAlignment: Text.AlignHCenter
        }
    }

    function updateGraphs() {
        latest = dataSource.latestSpo2();
        graphSpo2Normal.updateGraph(day);
        graphSpo2Sleep.updateGraph(day);
    }

    Component.onCompleted: {
        day = new Date();
        updateGraphs();
    }
}
