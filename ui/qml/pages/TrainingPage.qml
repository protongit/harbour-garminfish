import QtQuick 2.0
import QtQuick.Layouts 1.1
import uk.co.protongit.garminfish 1.0
import "../components/"
import "../components/platform"

PagePL {
    id: page
    title: qsTr("Training Load")

    property var stats: ({})

    pageMenu: PageMenuPL {
        PageMenuItemPL {
            iconSource: styler.iconRefresh !== undefined ? styler.iconRefresh : ""
            text: qsTr("Reload")
            onClicked: load()
        }
    }

    Column {
        id: column
        width: parent.width
        anchors.top: parent.top
        anchors.margins: styler.themePaddingMedium
        spacing: styler.themePaddingLarge

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: styler.themeFontSizeExtraLarge * 2
            text: qsTr("Acute %1").arg(fixed(stats.acute, 1))
            horizontalAlignment: Text.AlignHCenter
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: styler.themeFontSizeExtraLarge
            text: qsTr("Chronic %1").arg(fixed(stats.chronic, 1))
            horizontalAlignment: Text.AlignHCenter
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Load ratio: %1").arg(fixed(stats.ratio, 2))
            horizontalAlignment: Text.AlignHCenter
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            color: styler.themeSecondaryHighlightColor
            text: qsTr("Last: %1 (%2)").arg(stats.lastName || "-").arg(fixed(stats.lastLoad, 1))
            horizontalAlignment: Text.AlignHCenter
        }

        Graph {
            id: graphTrainingLoad
            graphTitle: qsTr("Daily load (last 10 days)")
            graphHeight: 300
            axisX.mask: "MM/dd"
            axisY.units: qsTr("Load")
            type: DataSource.TrainingLoadSummary
            graphType: bar
            minY: 0
            maxY: Math.max(10, suggestedMaxY)
            valueConverter: function(value) {
                return value.toFixed(1);
            }
            Component.onCompleted: updateGraph(new Date())
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            visible: graphTrainingLoad.noData
            color: styler.themeSecondaryHighlightColor
            text: qsTr("No training load data yet")
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            color: styler.themeSecondaryHighlightColor
            text: qsTr("Acute is the sum of the last 7 days. Chronic is the sum of the last 28 days. The ratio compares the 7-day and 28-day daily averages.")
        }

        ButtonPL {
            width: parent.width
            text: qsTr("Open activities")
            onClicked: app.pages.push(Qt.resolvedUrl("SportsSummaryPage.qml"))
        }
    }

    function fixed(value, digits) {
        if (value === undefined || value === null) {
            return "-";
        }
        return Number(value).toFixed(digits);
    }

    function load() {
        stats = dataSource.trainingLoadStats();
    }

    Component.onCompleted: load()
}
