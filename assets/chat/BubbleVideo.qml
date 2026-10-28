import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Container {
    id: videoBubble
    visible: !rowRoot.recalled
             && (ListItemData.msgType === 3 || ListItemData.msgType === "3")
    horizontalAlignment: HorizontalAlignment.Fill
    topMargin: 2; bottomMargin: 2
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
    maxWidth: rowRoot.bubbleMaxW

    // Use plain functions with ListItemData.content passed explicitly, not IIFE bindings
    // ((function(){...})()): the dependency on ListItemData.content inside the closure wasn't always
    // tracked, so a bubble created just as the message arrived could keep stale content (garbled
    // filename/href).
    function extractJsonStringField(content, key) {
        var c = content || "";
        if (c.length === 0 || c.charCodeAt(0) !== 123) return "";
        var k = '"' + key + '":"';
        var si = c.indexOf(k);
        if (si < 0) return "";
        si += k.length;
        var ei = si;
        while (ei < c.length) {
            var code = c.charCodeAt(ei);
            if (code === 92) { ei += 2; continue; }
            if (code === 34) break;
            ei++;
        }
        return c.substring(si, ei);
    }
    property string vFileName: videoBubble.extractJsonStringField(ListItemData.content, "fileName") || "video.mp4"
    property string vHref: videoBubble.extractJsonStringField(ListItemData.content, "href")
    function extFor(fileName) {
        var n = fileName || "";
        var di = n.lastIndexOf(".");
        return di >= 0 ? n.substring(di + 1).toLowerCase() : "";
    }
    function iconFor(ext) {
        if (ext === "txt")                 return "asset:///images/ChatView/bubble/file_types/File Type - TXT.png";
        if (ext === "doc" || ext === "docx") return "asset:///images/ChatView/bubble/file_types/File Type - Document.png";
        if (ext === "pdf")                 return "asset:///images/ChatView/bubble/file_types/File Type - PDF.png";
        if (ext === "ppt" || ext === "pptx") return "asset:///images/ChatView/bubble/file_types/File Type - PPT.png";
        if (ext === "xls" || ext === "xlsx") return "asset:///images/ChatView/bubble/file_types/File Type - XLS (Spreadsheet).png";
        if (ext === "epub")                return "asset:///images/ChatView/bubble/file_types/File Type - ePUB.png";
        if (ext === "apk")                 return "asset:///images/ChatView/bubble/file_types/File Type - APK.png";
        if (ext === "cer")                 return "asset:///images/ChatView/bubble/file_types/File Type - Certificate.png";
        if (ext === "zip" || ext === "rar" || ext === "7z") return "asset:///images/ChatView/bubble/file_types/File Type - ZIP (Compressed).png";
        if (ext === "vcf")                 return "asset:///images/ChatView/bubble/file_types/File Type - VCF (Contanct).png";
        if (ext === "mp3" || ext === "flac") return "asset:///images/ChatView/bubble/file_types/File Type - Music.png";
        if (ext === "m4a")                 return "asset:///images/ChatView/bubble/file_types/File Type - Voice Note (Audio Recording).png";
        if (ext === "bar")                 return "asset:///images/ChatView/bubble/file_types/File Type - BAR.png";
        return "asset:///images/ChatView/bubble/file_types/File Type - Video.png";
    }
    // Status text as a function with explicit parameters (block-body bindings are invalid, and
    // explicit parameters keep the dependency visible to the binding engine).
    function statusText(mine, msgId, uploadVideoActive, uploadVideoPercent,
                         uploadFileActive, uploadFilePercent,
                         downloading, progress, isVideo) {
        var mid = msgId || "";
        if (mine && mid.indexOf("local_video_") === 0) {
            return uploadVideoActive
                ? "Sending " + uploadVideoPercent + "%..."
                : "Sending...";
        }
        if (mine && mid.indexOf("local_file_") === 0) {
            return uploadFileActive
                ? "Sending " + uploadFilePercent + "%..."
                : "Sending...";
        }
        if (downloading) return "Downloading " + progress + "%...";
        return isVideo ? "Tap to play" : "Tap to open";
    }
    property string vExt: videoBubble.extFor(videoBubble.vFileName)
    property bool vIsVideo: videoBubble.vExt === "mp4" || videoBubble.vExt === "mov"
                          || videoBubble.vExt === "3gp" || videoBubble.vExt === "mkv"
    property string vIconSource: videoBubble.iconFor(videoBubble.vExt)
    property bool vDownloading: rowRoot.ListItem.view.videoProgressMsgId === (ListItemData.msgId || "")
                                 && rowRoot.ListItem.view.videoProgressMsgId !== ""
    property int  vProgress: vDownloading ? rowRoot.ListItem.view.videoProgressPercent : 0

    ImageView {
        imageSource: videoBubble.vIconSource
        scalingMethod: ScalingMethod.AspectFit
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: 68; preferredHeight: 68
        minWidth: 68
        rightMargin: 10
    }

    Container {
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center

        Label {
            text: videoBubble.vFileName
            horizontalAlignment: HorizontalAlignment.Fill
            multiline: true
            textStyle {
                base:  SystemDefaults.TextStyles.BodyText
                color: rowRoot.isDark ? Color.create("#eeeeee") : Color.create("#111111")
            }
        }
        Label {
            text: videoBubble.statusText(rowRoot.mine, ListItemData.msgId || "",
                      rowRoot.ListItem.view.uploadVideoActive, rowRoot.ListItem.view.uploadVideoPercent,
                      rowRoot.ListItem.view.uploadFileActive, rowRoot.ListItem.view.uploadFilePercent,
                      videoBubble.vDownloading, videoBubble.vProgress, videoBubble.vIsVideo)
            textStyle {
                fontSize: FontSize.XSmall
                fontStyle: FontStyle.Italic
                color: Color.create("#888888")
            }
            topMargin: 2
        }
    }

    gestureHandlers: [
        TapHandler {
            onTapped: {
                if (videoBubble.vDownloading) return;
                if (!videoBubble.vHref || videoBubble.vHref === "") return;
                rowRoot.ListItem.view.playVideoMsg(ListItemData.msgId, videoBubble.vHref, videoBubble.vFileName);
            }
        }
    ]
}
