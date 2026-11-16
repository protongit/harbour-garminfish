import QtQuick 2.0
import QtQuick.Layouts 1.1
import uk.co.protongit.garminfish 1.0
import "../components/"
import "../components/platform"

PagePL {
    id: page
    title: fileName.length > 0 ? fileName : qsTr("FIT file")

    property string filePath
    property string fileName
    property var info: ({})

    pageMenu: PageMenuPL {
        PageMenuItemPL {
            iconSource: styler.iconRefresh !== undefined ? styler.iconRefresh : ""
            text: qsTr("Reload")
            onClicked: load()
        }
        PageMenuItemPL {
            iconSource: styler.iconDownloadData !== undefined ? styler.iconDownloadData : ""
            text: qsTr("Export")
            onClicked: {
                var destination = dataSource.exportFitFile(filePath);
                if (destination.length > 0) {
                    app.showMessage(qsTr("Exported to %1").arg(destination));
                } else {
                    app.showMessage(qsTr("Export failed"));
                }
            }
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
            font.pixelSize: styler.themeFontSizeLarge
            text: info.name || fileName
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            color: styler.themeSecondaryHighlightColor
            text: qsTr("%1 - %2 - %3").arg(info.type || "-")
                  .arg(info.sizeText || "-")
                  .arg(info.modified || "-")
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            visible: info.valid === false
            color: "red"
            text: qsTr("Cannot read this FIT file: %1").arg(info.error || "-")
        }

        SectionHeaderPL {
            text: qsTr("FIT summary")
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Profile version: %1").arg(info.profileVersion || "-")
        }
        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Data size: %1 bytes").arg(info.dataSize || 0)
        }
        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Time range: %1 -> %2")
                  .arg(info.firstTimestamp || "-")
                  .arg(info.lastTimestamp || "-")
        }

        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("File type: %1").arg(value(info.fileId, "type", "-"))
        }
        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Manufacturer: %1  Product: %2")
                  .arg(value(info.fileId, "manufacturer", "-"))
                  .arg(value(info.fileId, "product", "-"))
        }
        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Serial: %1").arg(value(info.fileId, "serial", "-"))
        }
        LabelPL {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Created: %1").arg(value(info.fileId, "timeCreated", "-"))
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.session && info.session.valid === true

            SectionHeaderPL {
                text: qsTr("Activity session")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Sport: %1").arg(info.sportName || value(info.session, "profileName", "-"))
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Start: %1").arg(value(info.session, "start", "-"))
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Duration: %1 s  Distance: %2 m")
                      .arg(fixed(value(info.session, "timer", 0), 0))
                      .arg(fixed(value(info.session, "distance", 0), 1))
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Heart rate: %1 / %2 bpm  Calories: %3")
                      .arg(value(info.session, "averageHeartRate", "-"))
                      .arg(value(info.session, "maxHeartRate", "-"))
                      .arg(value(info.session, "calories", "-"))
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Training effect: %1 / %2  Load: %3")
                      .arg(fixed(value(info.session, "trainingEffect", 0), 1))
                      .arg(fixed(value(info.session, "anaerobicEffect", 0), 1))
                      .arg(fixed(value(info.session, "trainingLoad", 0), 1))
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("TSS: %1  Laps: %2  Avg stress: %3")
                      .arg(fixed(value(info.session, "trainingStressScore", 0), 1))
                      .arg(value(info.session, "laps", "-"))
                      .arg(value(info.session, "averageStress", "-"))
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.lastMonitoring && info.lastMonitoring.time !== undefined

            SectionHeaderPL {
                text: qsTr("Last monitoring sample")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("%1  HR %2  Cycles %3  Intensity %4")
                      .arg(value(info.lastMonitoring, "time", "-"))
                      .arg(value(info.lastMonitoring, "heartRate", "-"))
                      .arg(value(info.lastMonitoring, "cycles", "-"))
                      .arg(value(info.lastMonitoring, "intensity", "-"))
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.lastStress && info.lastStress.time !== undefined

            SectionHeaderPL {
                text: qsTr("Last stress sample")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("%1  Stress %2  Body energy %3")
                      .arg(value(info.lastStress, "time", "-"))
                      .arg(value(info.lastStress, "stress", "-"))
                      .arg(value(info.lastStress, "bodyEnergy", "-"))
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.sleepStages && info.sleepStages.length > 0

            SectionHeaderPL {
                text: qsTr("Sleep")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("%1 stages  %2 -> %3")
                      .arg(info.sleepStages ? info.sleepStages.length : 0)
                      .arg(info.sleepStages && info.sleepStages.length > 0
                           ? info.sleepStages[0].time : "-")
                      .arg(info.sleepStages && info.sleepStages.length > 0
                           ? info.sleepStages[info.sleepStages.length - 1].time : "-")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                visible: info.sleepStats && info.sleepStats.overallScore !== undefined
                text: qsTr("Score: %1  Quality: %2  Recovery: %3")
                      .arg(value(info.sleepStats, "overallScore", "-"))
                      .arg(value(info.sleepStats, "qualityScore", "-"))
                      .arg(value(info.sleepStats, "recoveryScore", "-"))
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.hrvSummary && (info.hrvSummary.lastNightAverage !== undefined
                                         || (info.hrvValues ? info.hrvValues.length : 0) > 0)

            SectionHeaderPL {
                text: qsTr("HRV / VFC")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                visible: info.hrvSummary && info.hrvSummary.lastNightAverage !== undefined
                text: qsTr("Last night: %1 ms  Weekly: %2 ms  Status: %3")
                      .arg(fixed(value(info.hrvSummary, "lastNightAverage", 0), 1))
                      .arg(fixed(value(info.hrvSummary, "weeklyAverage", 0), 1))
                      .arg(value(info.hrvSummary, "status", "-"))
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("%1 HRV values").arg(info.hrvValues ? info.hrvValues.length : 0)
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.metrics && info.metrics.time !== undefined

            SectionHeaderPL {
                text: qsTr("Metrics")
            }
            LabelPL {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Aerobic: %1  Anaerobic: %2  Recovery: %3 s")
                      .arg(fixed(value(info.metrics, "aerobicEffect", 0), 2))
                      .arg(fixed(value(info.metrics, "anaerobicEffect", 0), 2))
                      .arg(value(info.metrics, "recoveryTime", "-"))
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.records && info.records.length > 0

            SectionHeaderPL {
                text: qsTr("First records (%1 total)").arg(info.recordCount || 0)
            }
            Repeater {
                model: info.records || []
                LabelPL {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: qsTr("%1  %2, %3  HR %4  Dist %5")
                          .arg(modelData.time || "-")
                          .arg(modelData.latitude !== undefined ? modelData.latitude.toFixed(6) : "-")
                          .arg(modelData.longitude !== undefined ? modelData.longitude.toFixed(6) : "-")
                          .arg(modelData.heartRate !== undefined ? modelData.heartRate : "-")
                          .arg(modelData.distance !== undefined ? modelData.distance.toFixed(1) : "-")
                }
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.messages && info.messages.length > 0

            SectionHeaderPL {
                text: qsTr("Messages")
            }
            Repeater {
                model: info.messages || []
                LabelPL {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: styler.themeFontSizeSmall
                    text: qsTr("%1 (%2) x%3 - fields: %4")
                          .arg(modelData.name)
                          .arg(modelData.number)
                          .arg(modelData.count)
                          .arg(modelData.fields || "-")
                }
            }
        }

        Column {
            width: parent.width
            spacing: styler.themePaddingSmall
            visible: info.unknownMessages && info.unknownMessages.length > 0

            SectionHeaderPL {
                text: qsTr("Unknown messages")
            }
            Repeater {
                model: info.unknownMessages || []
                LabelPL {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: styler.themeFontSizeSmall
                    color: styler.themeSecondaryHighlightColor
                    text: qsTr("Message %1 x%2 - fields: %3")
                          .arg(modelData.number)
                          .arg(modelData.count)
                          .arg(modelData.fields || "-")
                }
            }
        }
    }

    function value(object, key, fallback) {
        if (object && object[key] !== undefined && object[key] !== null) {
            return object[key];
        }
        return fallback;
    }

    function fixed(value, digits) {
        if (value === undefined || value === null) {
            return "-";
        }
        return Number(value).toFixed(digits);
    }

    function load() {
        info = dataSource.inspectFitFile(filePath);
    }

    Component.onCompleted: load()
}
