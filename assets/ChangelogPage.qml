import bb.cascades 1.4
import QtQuick 1.0

// Pushed into aboutNav from AboutSheet.qml when the user taps "Change List".
// htmlContent is set right after creation (app.fetchChangelog() -> changelogReady(html)
// in AboutSheet.qml) and rendered with a WebView, so the Version-header + bullet-list
// formatting from app.buildChangelogHtml() can use real bold/spacing instead of being
// faked with plain Cascades Labels.
Page {
    id: changelogPage

    property string htmlContent: ""

    onHtmlContentChanged: {
        if (htmlContent.length > 0) {
            changelogWebView.loadHtml(htmlContent, "local:///changelog");
        }
    }

    titleBar: TitleBar {
        kind: TitleBarKind.FreeForm
        kindProperties: FreeFormTitleBarKindProperties {
            content: Container {
                background: Color.create("#2575fc")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                leftPadding: ui.du(2.5)
                layout: DockLayout {}
                Label {
                    text: "Change List"
                    horizontalAlignment: HorizontalAlignment.Left
                    verticalAlignment: VerticalAlignment.Center
                    textStyle {
                        color: Color.White
                        fontWeight: FontWeight.Bold
                        fontSize: FontSize.Large
                    }
                }
            }
        }
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        layout: DockLayout {}

        WebView {
            id: changelogWebView
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            visible: changelogPage.htmlContent.length > 0
        }

        ActivityIndicator {
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            running: changelogPage.htmlContent.length === 0
            visible: changelogPage.htmlContent.length === 0
        }
    }
}
