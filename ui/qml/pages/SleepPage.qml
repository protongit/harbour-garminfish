import QtQuick 2.0
import uk.co.piggz.amazfish 1.0
import QtQuick.Layouts 1.1
import "../components/"
import "../components/platform"

PagePL {
    id: page
    title: qsTr("Sleep")

    property alias day: nav.day
    property real sleepTotal: 0
    property real sleepLight: 0
    property real sleepDeep: 0
    property real sleepRem: 0
    property real sleepAwake: 0
    property real sleepInBed: 0
    property string intervalLabel: ""
    property var sleepSummaryStats: ({})
    property var weeklyHistory: []
    property var nightPhases: []

    // Garmin's sleep chart colours.
    readonly property color colorDeep: "#2f5d9e"
    readonly property color colorLight: "#5aa9e6"
    readonly property color colorRem: "#9d7bd8"
    readonly property color colorAwake: "#e0a136"
    readonly property color colorUnmeasurable: "#808080"

    pageMenu: PageMenuPL {
        DownloadDataMenuItem{}
    }

    Column {
        id: column
        width: parent.width
        spacing: styler.themePaddingMedium

        LabelPL {
            id: lblSleepLastnight
            font.pixelSize: styler.themeFontSizeExtraLarge * 2
            width: parent.width
            text: qsTr("Total %1").arg(decimalToHourMin(sleepTotal))
            horizontalAlignment: Text.AlignHCenter
        }

        LabelPL {
            id: lblInterval
            width: parent.width
            font.pixelSize: styler.themeFontSizeMedium
            color: styler.themeSecondaryHighlightColor
            text: intervalLabel
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
        }

        Row {
            width: parent.width
            spacing: styler.themePaddingMedium

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: styler.themePaddingSmall

                LabelPL {
                    font.pixelSize: styler.themeFontSizeLarge
                    color: page.colorDeep
                    text: qsTr("Deep %1").arg(decimalToHourMin(sleepDeep))
                }
                LabelPL {
                    font.pixelSize: styler.themeFontSizeLarge
                    color: page.colorRem
                    text: qsTr("REM %1").arg(decimalToHourMin(sleepRem))
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: styler.themePaddingSmall

                LabelPL {
                    font.pixelSize: styler.themeFontSizeLarge
                    color: page.colorLight
                    text: qsTr("Light %1").arg(decimalToHourMin(sleepLight))
                }
                LabelPL {
                    font.pixelSize: styler.themeFontSizeLarge
                    color: page.colorAwake
                    text: qsTr("Awake %1").arg(decimalToHourMin(sleepAwake))
                }
            }
        }

        LabelPL {
            id: lblSleepScore
            width: parent.width
            wrapMode: Text.Wrap
            visible: sleepSummaryStats.overallScore !== undefined
            text: qsTr("Score %1 · %2")
                  .arg(sleepSummaryStats.overallScore || 0)
                  .arg(qualityLabel(sleepSummaryStats.overallScore))
            horizontalAlignment: Text.AlignHCenter
        }

        LabelPL {
            id: lblSleepRecovery
            width: parent.width
            font.pixelSize: styler.themeFontSizeSmall
            color: styler.themeSecondaryHighlightColor
            visible: sleepSummaryStats.recoveryScore !== undefined
            text: qsTr("Recovery %1 · Avg stress %2")
                  .arg(sleepSummaryStats.recoveryScore || 0)
                  .arg(Number(sleepSummaryStats.averageStress || 0).toFixed(0))
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

        LabelPL {
            width: parent.width
            font.pixelSize: styler.themeFontSizeLarge
            color: styler.themeHighlightColor
            text: qsTr("Sleep phases")
        }

        Canvas {
            id: nightChart
            width: parent.width
            height: 170
            property var phases: page.nightPhases
            onPhasesChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                if (!phases || phases.length === 0) {
                    return;
                }

                var left = 0;
                var right = width;
                var top = 4;
                var bottom = height - 18;
                var rowHeight = (bottom - top) / 4;
                var start = phases[0].start;
                var end = phases[phases.length - 1].end;
                var span = end - start;
                if (span <= 0) {
                    return;
                }

                // Hour grid.
                ctx.strokeStyle = Qt.rgba(0.6, 0.6, 0.6, 0.35);
                ctx.lineWidth = 1;
                ctx.fillStyle = styler.themeSecondaryHighlightColor;
                ctx.font = "11px sans-serif";
                ctx.textAlign = "center";
                var hour = Math.ceil(start / 3600) * 3600;
                for (; hour <= end; hour += 3600) {
                    var hx = left + (hour - start) / span * (right - left);
                    ctx.beginPath();
                    ctx.moveTo(hx, top);
                    ctx.lineTo(hx, bottom);
                    ctx.stroke();
                    ctx.fillText(Qt.formatDateTime(new Date(hour * 1000), "hh:mm"),
                                 hx, height - 4);
                }

                // One row per stage, top to bottom: awake, REM, light, deep.
                for (var i = 0; i < phases.length; ++i) {
                    var phase = phases[i];
                    var row = 3;
                    var color = colorUnmeasurable;
                    if (phase.stage === 1) {
                        row = 0;
                        color = colorAwake;
                    } else if (phase.stage === 4) {
                        row = 1;
                        color = colorRem;
                    } else if (phase.stage === 2) {
                        row = 2;
                        color = colorLight;
                    } else if (phase.stage === 3) {
                        row = 3;
                        color = colorDeep;
                    }

                    var x1 = left + (phase.start - start) / span * (right - left);
                    var x2 = left + (phase.end - start) / span * (right - left);
                    ctx.fillStyle = color;
                    ctx.fillRect(x1, top + row * rowHeight + 2,
                                 Math.max(x2 - x1, 1), rowHeight - 4);
                }
            }
        }

        LabelPL {
            width: parent.width
            font.pixelSize: styler.themeFontSizeLarge
            color: styler.themeHighlightColor
            text: qsTr("Sleep score · last 7 days")
        }

        Canvas {
            id: scoreChart
            width: parent.width
            height: 150
            property var history: page.weeklyHistory
            onHistoryChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                if (!history || history.length === 0) {
                    return;
                }

                var left = 26;
                var right = width;
                var top = 14;
                var bottom = height - 18;
                var count = history.length;
                var slot = (right - left) / count;
                var barWidth = Math.min(slot * 0.55, 34);

                ctx.font = "10px sans-serif";
                ctx.strokeStyle = Qt.rgba(0.6, 0.6, 0.6, 0.3);
                ctx.fillStyle = styler.themeSecondaryHighlightColor;
                ctx.textAlign = "right";
                for (var g = 0; g <= 4; ++g) {
                    var value = g * 25;
                    var gy = bottom - (bottom - top) * value / 100;
                    ctx.beginPath();
                    ctx.moveTo(left, gy);
                    ctx.lineTo(right, gy);
                    ctx.stroke();
                    ctx.fillText(String(value), left - 4, gy + 3);
                }

                ctx.textAlign = "center";
                for (var i = 0; i < count; ++i) {
                    var entry = history[i];
                    var cx = left + slot * (i + 0.5);
                    var score = Number(entry.score);

                    if (score >= 0) {
                        var barHeight = (bottom - top) * score / 100;
                        ctx.fillStyle = styler.themeHighlightColor;
                        ctx.fillRect(cx - barWidth / 2, bottom - barHeight,
                                     barWidth, barHeight);
                        ctx.fillStyle = styler.themePrimaryColor;
                        ctx.fillText(String(score), cx, bottom - barHeight - 4);
                    }

                    ctx.fillStyle = styler.themeSecondaryHighlightColor;
                    ctx.fillText(Qt.formatDate(entry.date, "ddd"), cx, height - 4);
                }
            }
        }

        LabelPL {
            width: parent.width
            font.pixelSize: styler.themeFontSizeLarge
            color: styler.themeHighlightColor
            text: qsTr("Sleep phases · last 7 days")
        }

        Canvas {
            id: stagesChart
            width: parent.width
            height: 170
            property var history: page.weeklyHistory
            onHistoryChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                if (!history || history.length === 0) {
                    return;
                }

                var left = 26;
                var right = width;
                var top = 12;
                var bottom = height - 18;
                var count = history.length;
                var slot = (right - left) / count;
                var barWidth = Math.min(slot * 0.55, 34);

                var maxTotal = 8;
                for (var i = 0; i < count; ++i) {
                    maxTotal = Math.max(maxTotal, Number(history[i].total || 0)
                                                + Number(history[i].awake || 0));
                }

                ctx.font = "10px sans-serif";
                ctx.strokeStyle = Qt.rgba(0.6, 0.6, 0.6, 0.3);
                ctx.fillStyle = styler.themeSecondaryHighlightColor;
                ctx.textAlign = "right";
                for (var g = 0; g <= 2; ++g) {
                    var hours = g * maxTotal / 2;
                    var gy = bottom - (bottom - top) * hours / maxTotal;
                    ctx.beginPath();
                    ctx.moveTo(left, gy);
                    ctx.lineTo(right, gy);
                    ctx.stroke();
                    ctx.fillText(hours.toFixed(0) + "h", left - 4, gy + 3);
                }

                for (var j = 0; j < count; ++j) {
                    var entry = history[j];
                    var cx = left + slot * (j + 0.5);
                    var scale = (bottom - top) / maxTotal;
                    var y = bottom;
                    var parts = [
                        { value: Number(entry.deep || 0), color: page.colorDeep },
                        { value: Number(entry.light || 0), color: page.colorLight },
                        { value: Number(entry.rem || 0), color: page.colorRem },
                        { value: Number(entry.awake || 0), color: page.colorAwake }
                    ];

                    for (var k = 0; k < parts.length; ++k) {
                        var height = parts[k].value * scale;
                        if (height <= 0) {
                            continue;
                        }
                        ctx.fillStyle = parts[k].color;
                        ctx.fillRect(cx - barWidth / 2, y - height, barWidth, height);
                        y -= height;
                    }

                    ctx.fillStyle = styler.themeSecondaryHighlightColor;
                    ctx.textAlign = "center";
                    ctx.fillText(Qt.formatDate(entry.date, "ddd"), cx, height - 4);
                }
            }
        }
    }

    function qualityLabel(score) {
        if (score === undefined || score === null || score < 0) {
            return "";
        }
        if (score < 60) {
            return qsTr("Deficiente");
        }
        if (score < 80) {
            return qsTr("Regular");
        }
        if (score < 90) {
            return qsTr("Bueno");
        }
        return qsTr("Excelente");
    }

    function updateGraphs() {
        var breakdown = dataSource.sleepBreakdown(day);
        sleepLight = breakdown.light || 0;
        sleepDeep = breakdown.deep || 0;
        sleepRem = breakdown.rem || 0;
        sleepAwake = breakdown.awake || 0;
        sleepTotal = breakdown.total || 0;
        sleepInBed = breakdown.inBed || 0;

        var start = breakdown.intervalStart;
        var end = breakdown.intervalEnd;
        if (start && end && !isNaN(new Date(start).getTime())) {
            intervalLabel = Qt.formatDateTime(start, "hh:mm") + " - "
                    + Qt.formatDateTime(end, "hh:mm");
        } else {
            intervalLabel = "";
        }

        sleepSummaryStats = dataSource.sleepStats(day);
        nightPhases = dataSource.sleepPhases(day);
        weeklyHistory = dataSource.sleepHistory(7);

        nightChart.requestPaint();
        scoreChart.requestPaint();
        stagesChart.requestPaint();
    }

    function decimalToHourMin(decTime) {
        var totalMinutes = Math.round(decTime * 60);
        var hours = Math.floor(totalMinutes / 60);
        var minutes = (totalMinutes % 60);
        if (minutes < 10) {
            return hours + ":0" + minutes;
        }

        return hours + ":" + minutes;
    }

    Component.onCompleted: {
        // Garmin stores sleep keyed by the night, so open the page on the
        // latest night that actually has data instead of always today.
        var latest = dataSource.latestSleepDate();
        var latestDate = new Date(latest);
        if (latest && !isNaN(latestDate.getTime())) {
            day = latestDate;
        } else {
            day = new Date();
        }
        updateGraphs();
    }
}
