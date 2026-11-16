import QtQuick 2.0
import QtQuick.Layouts 1.1
import uk.co.piggz.amazfish 1.0
import "../components/"
import "../components/platform"

PagePL {
    id: page
    title: activitytitle.length > 0 ? activitytitle : qsTr("Activity charts")

    property int activityId
    property string kindstring
    property string activitytitle
    property string tcx
    property var laps: []

    property bool paceRelevant: {
        var kind = kindstring.toLowerCase();
        return kind.indexOf("run") >= 0
                || kind.indexOf("walk") >= 0
                || kind.indexOf("hik") >= 0;
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
            color: styler.themeSecondaryHighlightColor
            text: qsTr("The charts use the FIT/GPX track stored for this activity.")
        }

        GraphData {
            id: graphHeartRate
            width: parent.width
            graphTitle: qsTr("Heart rate")
            graphHeight: 200
            axisX.mask: "hh:mm"
            axisY.units: qsTr("bpm")
            graphType: line
            minY: 0
            scale: true
            valueConverter: function(value) {
                return value.toFixed(0);
            }
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            visible: graphHeartRate.noData
            color: styler.themeSecondaryHighlightColor
            text: qsTr("No heart rate data in this activity")
        }

        GraphData {
            id: graphPace
            width: parent.width
            graphTitle: page.paceRelevant ? qsTr("Pace") : qsTr("Speed")
            graphHeight: 200
            axisX.mask: "hh:mm"
            axisY.units: page.paceRelevant ? qsTr("min/km") : qsTr("km/h")
            graphType: line
            minY: 0
            scale: true
            valueConverter: function(value) {
                return value.toFixed(1);
            }
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            visible: graphPace.noData
            color: styler.themeSecondaryHighlightColor
            text: page.paceRelevant
                  ? qsTr("No pace data in this activity")
                  : qsTr("No speed data in this activity")
        }

        SectionHeaderPL {
            text: qsTr("Laps")
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            visible: laps.length === 0
            color: styler.themeSecondaryHighlightColor
            text: qsTr("No laps stored for this activity")
        }

        Repeater {
            model: laps
            Column {
                width: column.width
                spacing: styler.themePaddingSmall

                LabelPL {
                    width: parent.width
                    font.pixelSize: styler.themeFontSizeLarge
                    text: qsTr("Lap %1").arg(modelData.index + 1)
                }
                LabelPL {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: styler.themeSecondaryHighlightColor
                    text: qsTr("%1 · %2 km · %3")
                          .arg(fncCovertSecondsToString(modelData.timer || modelData.elapsed || 0))
                          .arg(((modelData.distance || 0) / 1000.0).toFixed(2))
                          .arg(page.paceRelevant
                               ? qsTr("Pace %1").arg(paceString(modelData.pace))
                               : qsTr("Speed %1 km/h").arg(((modelData.averageSpeed || 0) * 3.6).toFixed(1)))
                }
                LabelPL {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: styler.themeSecondaryHighlightColor
                    text: qsTr("HR %1 / %2 bpm · %3 kcal")
                          .arg(modelData.averageHeartRate || "-")
                          .arg(modelData.maxHeartRate || "-")
                          .arg(modelData.calories || "-")
                }
            }
        }
    }

    TrackLoader {
        id: trackLoader
        onTrackChanged: buildCharts()
    }

    function buildCharts() {
        var hrPoints = [];
        var pacePoints = [];
        var count = trackLoader.trackPointCount();
        for (var i = 0; i < count; i++) {
            var x = trackLoader.unixTimeAt(i);
            var hr = trackLoader.heartRateAt(i);
            if (hr > 0) {
                hrPoints.push({ "x": x, "y": hr });
            }
            if (page.paceRelevant) {
                var pace = trackLoader.paceAt(i);
                if (pace > 0 && pace < 99) {
                    pacePoints.push({ "x": x, "y": pace });
                }
            } else {
                var speed = trackLoader.speedAt(i);
                if (speed > 0) {
                    pacePoints.push({ "x": x, "y": speed });
                }
            }
        }
        graphHeartRate.setPoints(hrPoints);
        graphPace.setPoints(pacePoints);
    }

    function fncCovertSecondsToString(sec) {
        var iHours = Math.floor(sec / 3600);
        var iMinutes = Math.floor((sec - iHours * 3600) / 60);
        var iSeconds = Math.floor(sec - (iHours * 3600) - (iMinutes * 60));
        return (iHours > 0 ? iHours + "h " : "") + (iMinutes > 0 ? iMinutes + "m " : "") + iSeconds + "s";
    }

    function paceString(pace) {
        if (!pace || pace <= 0 || pace >= 99) {
            return "-";
        }
        var minutes = Math.floor(pace);
        var seconds = Math.round((pace - minutes) * 60);
        if (seconds === 60) {
            minutes += 1;
            seconds = 0;
        }
        return minutes + ":" + (seconds < 10 ? "0" + seconds : seconds);
    }

    Component.onCompleted: {
        laps = dataSource.activityLaps(activityId);
        trackLoader.loadString(tcx);
    }
}
