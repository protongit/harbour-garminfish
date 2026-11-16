import QtQuick 2.0
import uk.co.protongit.garminfish 1.0
import "../components"
import "../components/platform"

PagePL {
    id: page
    title: qsTr("Analysis")
    property alias day: nav.day
    property var hrvSummary: ({})

    pageMenu: PageMenuPL {
        PageMenuItemPL {
            iconSource: styler.iconDownloadData !== undefined ? styler.iconDownloadData : ""
            text: qsTr("Download All Data")
            onClicked: DaemonInterfaceInstance.fetchFullData(Amazfish.TYPE_HRV | Amazfish.TYPE_TEMPERATURE | Amazfish.TYPE_ACTIVITY);
        }
    }

    // Place our content in a Column.  The PageHeader is always placed at the top
    // of the page, followed by our content.
    Column {
        id: column
        width: parent.width
        anchors.top: parent.top
        anchors.margins: styler.themePaddingMedium
        spacing: styler.themePaddingLarge

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
            id: graphHeartrate
            graphTitle: qsTr("Heartrate")
            graphHeight: 300

            axisY.units: qsTr("BPM")
            type: DataSource.Heartrate
            visible: supportsFeatureRefresh(Amazfish.TYPE_HEART_RATE)

            minY: 0
            maxY: 200
            valueConverter: function(value) {
                return value.toFixed(0);
            }
            onClicked: {
                updateGraph(day);
            }
        }
        LabelPL {
            text: qsTr("Latest steps: %1").arg(isToday()
                                              ? _InfoSteps.toLocaleString()
                                              : graphSteps.lastValue.toLocaleString())
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
        }

        Graph {
            id: graphSteps
            graphTitle: qsTr("Steps")
            graphHeight: 300

            axisX.mask: "MM/dd"
            axisY.units: qsTr("Steps")
            type: DataSource.StepSummary
            graphType: bar

            minY: 0
            maxY: (2 * AmazfishConfig.profileFitnessGoal > suggestedMaxY)
                  ? 2 * AmazfishConfig.profileFitnessGoal
                  : Math.ceil(suggestedMaxY/1000)*1000
            valueConverter: function(value) {
                return value.toFixed(0);
            }
            onClicked: {
                updateGraph(day);
            }
        }
        Graph {
            id: graphIntensity
            graphTitle: qsTr("Intensity")
            graphHeight: 300

            axisY.units: "%"
            type: DataSource.Intensity

            minY: 0
            maxY: 100
            valueConverter: function(value) {
                return value.toFixed(0);
            }
            onClicked: {
                updateGraph(day);
            }
        }
        LabelPL {
            id: lblHrvSummary
            width: parent.width
            wrapMode: Text.Wrap
            color: styler.themeSecondaryHighlightColor
            text: qsTr("No HRV summary yet")
        }

        Graph {
            id: graphHRV
            graphTitle: qsTr("HRV / VFC")
            graphHeight: 300

            axisY.units: ""
            type: DataSource.HRV

            visible: supportsDataRefresh(Amazfish.TYPE_HRV)

            minY: 0
            maxY: 100

            onClicked: {
                updateGraph(day);
            }
        }

        Graph {
            id: graphBodyTemperature
            graphTitle: qsTr("Body Temperature")
            graphHeight: 300

            axisY.units: "°C"
            axisX.mask: "hh:mm"

            visible: supportsDataRefresh(Amazfish.TYPE_TEMPERATURE)

            type: DataSource.BodyTemperature
            graphType: line

            minY: -20
            maxY: 50

            onClicked: {
                updateGraph(day);
            }
        }

    }

    function isToday() {
        var now = new Date();
        return day.getFullYear() === now.getFullYear()
                && day.getMonth() === now.getMonth()
                && day.getDate() === now.getDate();
    }

    function updateHrvSummary() {
        hrvSummary = dataSource.latestHrvSummary();
        if (hrvSummary === undefined || hrvSummary.lastNightAverage === undefined) {
            lblHrvSummary.text = qsTr("No HRV summary yet");
            return;
        }
        lblHrvSummary.text = qsTr("Last night: %1 ms · 7-day avg: %2 ms · Status: %3")
                .arg(Math.round(hrvSummary.lastNightAverage))
                .arg(Math.round(hrvSummary.weeklyAverage))
                .arg(hrvSummary.status);
        if (hrvSummary.baselineBalancedLow !== undefined) {
            lblHrvSummary.text += qsTr(" · Baseline: %1-%2 ms")
                    .arg(Math.round(hrvSummary.baselineBalancedLow))
                    .arg(Math.round(hrvSummary.baselineBalancedHigh));
        }
    }

    function updateGraphs() {
        graphHeartrate.updateGraph(day);
        graphSteps.updateGraph(day);
        graphIntensity.updateGraph(day);
        graphHRV.updateGraph(day);
        graphBodyTemperature.updateGraph(day);
        updateHrvSummary();
    }

    Component.onCompleted: {
        day = new Date();
        updateGraphs();
    }
}
