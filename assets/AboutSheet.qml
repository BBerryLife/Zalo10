import bb.cascades 1.4
import bb.system 1.0
import QtQuick 1.0

Sheet {
    id: aboutSheetRoot
    property variant zService: null

    NavigationPane {
        id: aboutNav

        property variant activeChangelogPage: null
        property string pendingManifestAction: ""
        property string pendingDownloadUrl: ""
        property string pendingDownloadVersion: ""

        function isVersionNewer(a, b) {
            var pa = a.split('.');
            var pb = b.split('.');
            var n = Math.max(pa.length, pb.length);
            for (var i = 0; i < n; i++) {
                var va = parseInt(pa[i] || "0", 10);
                var vb = parseInt(pb[i] || "0", 10);
                if (isNaN(va)) va = 0;
                if (isNaN(vb)) vb = 0;
                if (va !== vb) return va > vb;
            }
            return false;
        }

        // Earlier attempts rendered the changelog as one HTML string in a WebView,
        // sized either by asking the WebView to measure its own content
        // (document.body.scrollHeight) or by a hand-estimated pixel height. Both
        // under-sized the WebView on device and cut the list off partway through —
        // this WebView engine just isn't a reliable source of its own content height.
        // This now builds plain row data for a native ListView (same pattern as
        // ChatsTab/GroupsTab/InvitesTab), whose scrolling already works correctly
        // everywhere else in this app. The leading NEW:/IMPROVE:/FIX:/REMOVED: tag is
        // split out so it can be rendered bold via a separate Label (mirrors the
        // bold-name + plain-preview pattern ChatsTab already uses); markdown "**"
        // emphasis elsewhere in a line is stripped to plain text rather than guessing
        // at an unverified rich-text Label API.
        function buildChangelogRows(versions) {
            var tags = ["NEW:", "IMPROVE:", "FIX:", "REMOVED:"];
            var rows = [];
            for (var i = 0; i < versions.length; i++) {
                var v = versions[i] || {};
                var ver = v.version || "";
                var items = v.items || [];
                rows.push({ rowType: "header", version: ver, isFirstHeader: (i === 0) });
                for (var j = 0; j < items.length; j++) {
                    var raw = items[j];
                    var tag = "";
                    var rest = raw;
                    for (var t = 0; t < tags.length; t++) {
                        if (raw.indexOf(tags[t]) === 0) {
                            tag = tags[t];
                            rest = raw.substring(tags[t].length);
                            break;
                        }
                    }
                    rest = rest.split("**").join("");
                    rows.push({ rowType: "item", tag: tag, text: rest });
                }
            }
            return rows;
        }

        function onManifestLoaded(manifest) {
            var action = aboutNav.pendingManifestAction;
            aboutNav.pendingManifestAction = "";

            if (action === "update") {
                updateBtn.enabled = true;
                var latest = manifest.latestVersion || "";
                var downloadUrl = manifest.downloadUrl || "";
                if (latest.length === 0) {
                    updateResultToast.body = "Update info unavailable right now. Try again later.";
                    updateResultToast.show();
                    return;
                }
                var current = app.appVersion();
                if (!aboutNav.isVersionNewer(latest, current)) {
                    updateResultToast.body = "You're already on the latest version (" + current + ").";
                    updateResultToast.show();
                } else {
                    // Store pending download info and show confirmation dialog
                    aboutNav.pendingDownloadUrl = downloadUrl;
                    aboutNav.pendingDownloadVersion = latest;
                    updateConfirmDialog.title = "New Update Available";
                    updateConfirmDialog.body = "New version " + latest + " detected. Would you like to download it?";
                    updateConfirmDialog.show();
                }
            } else if (action === "changelog") {
                var versions = manifest.changelog || [];
                if (aboutNav.activeChangelogPage) {
                    aboutNav.activeChangelogPage.setRows(aboutNav.buildChangelogRows(versions));
                }
            }
        }

        function onManifestFailed(message) {
            var action = aboutNav.pendingManifestAction;
            aboutNav.pendingManifestAction = "";

            if (action === "update") {
                updateBtn.enabled = true;
                updateResultToast.body = message;
                updateResultToast.show();
            } else if (action === "changelog") {
                if (aboutNav.activeChangelogPage) {
                    aboutNav.activeChangelogPage.setRows([{ rowType: "item", tag: "", text: message }]);
                }
            }
        }

        Page {
            titleBar: TitleBar {
                scrollBehavior: TitleBarScrollBehavior.Sticky
                kind: TitleBarKind.FreeForm
                kindProperties: FreeFormTitleBarKindProperties {
                    content: Container {
                        background: Color.create("#2575fc")
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment:   VerticalAlignment.Fill
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        leftPadding: ui.du(1)

                        ImageButton {
                            verticalAlignment: VerticalAlignment.Center
                            preferredWidth:  ui.du(6); preferredHeight: ui.du(6)
                            defaultImageSource: "asset:///images/AboutSheet/ic_close_white.png"
                            pressedImageSource: "asset:///images/AboutSheet/ic_close_white.png"
                            rightMargin: ui.du(0.5)
                            onClicked: { aboutSheetRoot.close() }
                        }

                        Label {
                            text: "About"
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            verticalAlignment: VerticalAlignment.Center
                            textStyle { color: Color.White; base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.Bold }
                            topMargin: 0; bottomMargin: 0
                        }
                    }
                }
            }

            actions: [
                ActionItem {
                    title: "Facebook"
                    imageSource: "asset:///images/AboutSheet/ic_facebook.png"
                    ActionBar.placement: ActionBarPlacement.OnBar
                    onTriggered: { Qt.openUrlExternally("https://www.facebook.com/BBerrylife") }
                },
                ActionItem {
                    title: "Donate"
                    imageSource: "asset:///images/AboutSheet/ic_scan_barcode.png"
                    ActionBar.placement: ActionBarPlacement.Signature
                    onTriggered: {
                        var donatePage = donatePageDef.createObject();
                        aboutNav.push(donatePage);
                    }
                },
                ActionItem {
                    title: "Website"
                    imageSource: "asset:///images/AboutSheet/ic_sb_network.png"
                    ActionBar.placement: ActionBarPlacement.OnBar
                    onTriggered: { Qt.openUrlExternally("https://BBerryLife.github.io") }
                }
            ]

            ScrollView {
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: 50; rightPadding: 50; topPadding: 50; bottomPadding: 80

                    Container {
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                        horizontalAlignment: HorizontalAlignment.Fill

                        Container {
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            verticalAlignment: VerticalAlignment.Center
                            Label { text: "Zalo10"; textStyle.base: SystemDefaults.TextStyles.BigText }
                            Label { text: "Version: " + app.appVersion(); textStyle.color: Color.Gray; topMargin: 4 }
                            Label { text: "Developed by BerryLife© 2026"; textStyle.color: Color.Gray; topMargin: 4 }
                        }

                        ImageView {
                            imageSource: "asset:///images/AboutSheet/berrylife.png"
                            scalingMethod: ScalingMethod.AspectFit
                            preferredWidth:  160
                            preferredHeight: 160
                            verticalAlignment: VerticalAlignment.Center
                            horizontalAlignment: HorizontalAlignment.Right
                        }
                    }

                    Divider { topMargin: 30; bottomMargin: 20 }

                    Label {
                        text: "Zalo10 is a native Zalo client for BlackBerry 10.\nNo NodeJS, no browser required."
                        multiline: true
                        textStyle.color: Color.Gray
                        horizontalAlignment: HorizontalAlignment.Center
                        textStyle.textAlign: TextAlign.Center
                    }

                    Container {
                        topMargin: 30
                        horizontalAlignment: HorizontalAlignment.Fill
                        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }

                        Button {
                            id: updateBtn
                            text: "Update"
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            rightMargin: ui.du(0.8)
                            onClicked: {
                                updateBtn.enabled = false;
                                updateCheckingToast.show();
                                aboutNav.pendingManifestAction = "update";
                                // Cache-bust: jsDelivr sends a max-age cache-control header,
                                // so the device's own WebView/network cache can keep serving an
                                // old copy long after the CDN side has already been purged.
                                // A changing query string makes every fetch look like a brand
                                // new URL the device has never cached.
                                manifestFetcher.url = manifestFetcher.manifestUrl + "?t=" + new Date().getTime();
                            }
                        }

                        Button {
                            id: changeListBtn
                            text: "Change List"
                            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                            leftMargin: ui.du(0.8)
                            onClicked: {
                                var changelogPage = changelogPageDef.createObject();
                                aboutNav.activeChangelogPage = changelogPage;
                                aboutNav.push(changelogPage);
                                aboutNav.pendingManifestAction = "changelog";
                                manifestFetcher.url = manifestFetcher.manifestUrl + "?t=" + new Date().getTime();
                            }
                        }
                    }

                    // Hidden — used purely to fetch Data/Zalo10-version.json through
                    // BB10's WebView/browser TLS stack instead of QNetworkAccessManager.
                    // The bare Qt4 networking stack fails the TLS handshake against
                    // GitHub's CDN (SslHandshakeFailedError) no matter what
                    // QSslConfiguration is thrown at it; the WebView's engine is kept
                    // current through OS updates independently of the frozen NDK Qt/OpenSSL.
                    WebView {
                        id: manifestFetcher
                        visible: false
                        enabled: false
                        preferredWidth: 1
                        preferredHeight: 1

                        // raw.githubusercontent.com sends a CSP "sandbox" directive on
                        // every response, which makes BB10's WebKit engine treat the
                        // loaded document as an opaque origin — evaluateJavaScript then
                        // can't reliably read document.body, so JSON.parse() gets back
                        // an empty/garbled string ("Unable to parse JSON string || got:").
                        // jsDelivr mirrors GitHub repo files without that header, so the
                        // same WebView approach actually works against it.
                        property string manifestUrl: "https://cdn.jsdelivr.net/gh/BBerryLife/BBerryLife.github.io@main/Data/Zalo10-version.json"

                        onLoadingChanged: {
                            if (loadRequest.status === WebLoadStatus.Succeeded) {
                                manifestFetcher.evaluateJavaScript("document.body.textContent || document.body.innerText || ''");
                            } else if (loadRequest.status === WebLoadStatus.Failed) {
                                aboutNav.onManifestFailed("Could not load update info. Check your internet connection.");
                            }
                        }

                        onJavaScriptResult: {
                            if (aboutNav.pendingManifestAction.length === 0) return;
                            try {
                                var manifest = JSON.parse(result);
                                aboutNav.onManifestLoaded(manifest);
                            } catch (e) {
                                // Surface what we actually got back instead of a generic
                                // "malformed" message — needed to diagnose why JSON.parse
                                // is failing (wrong field name? extra wrapper markup? etc).
                                var preview = (typeof result === "undefined") ? "<undefined>" : String(result).substring(0, 200);
                                aboutNav.onManifestFailed("Parse failed: " + e + " || got: " + preview);
                            }
                        }
                    }
                }
            }
        }

        attachedObjects: [
            // Wire up ZaloService download signals for the in-app update downloader.
            // target starts as null — service is a C++ context property that isn't
            // resolvable as a QML binding at Sheet init time. It IS accessible in
            // JS function bodies, so we set the target explicitly there instead.
            Connections {
                id: svcDownloadCon
                target: null
                onUpdateDownloadProgress: {
                    updateProgressDialog.progress = percent;
                }
                onUpdateDownloadFinished: {
                    updateProgressDialog.cancel();
                    updateResultToast.body = "Update downloaded. Check your Downloads folder.";
                    updateResultToast.show();
                }
                onUpdateDownloadFailed: {
                    updateProgressDialog.cancel();
                    updateResultToast.body = "Download failed: " + errorMsg;
                    updateResultToast.show();
                }
            },
            ComponentDefinition {
                id: donatePageDef
                Page {
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
                                    text: "Donate & Support"
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

                    ScrollView {
                        Container {
                            horizontalAlignment: HorizontalAlignment.Fill
                            leftPadding: 30; rightPadding: 30; topPadding: 50; bottomPadding: 50

                            Label {
                                text: "If you find the app useful and are feeling generous, you can donate 10,000 VND to help me fund the purchase of BB10/BBOS devices for app testing, and... to support the app developer."
                                multiline: true
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle {
                                    base: SystemDefaults.TextStyles.BodyText
                                    textAlign: TextAlign.Center
                                    lineHeight: 1.3
                                }
                            }

                            Container {
                                topMargin: 50
                                horizontalAlignment: HorizontalAlignment.Center
                                preferredWidth: ui.du(40)
                                preferredHeight: ui.du(40)
                                background: Color.White
                                leftPadding: 10; rightPadding: 10; topPadding: 10; bottomPadding: 10

                                ImageView {
                                    imageSource: "asset:///images/AboutSheet/barcode.png"
                                    scalingMethod: ScalingMethod.AspectFit
                                    horizontalAlignment: HorizontalAlignment.Fill
                                    verticalAlignment: VerticalAlignment.Fill
                                }
                            }

                            Label {
                                topMargin: 30
                                text: "Thank you for your support!"
                                horizontalAlignment: HorizontalAlignment.Center
                                textStyle {
                                    color: Color.DarkGray
                                    fontWeight: FontWeight.Bold
                                }
                            }
                        }
                    }
                }
            },
            ComponentDefinition {
                id: changelogPageDef
                source: "asset:///ChangelogPage.qml"
            },
            SystemToast {
                id: updateCheckingToast
                body: "Checking for updates…"
            },
            SystemToast {
                id: updateResultToast
                body: ""
            },
            // Confirmation dialog: shown when a newer version is found.
            // User can choose to download or cancel.
            SystemDialog {
                id: updateConfirmDialog
                title: "New Update Available"
                body: ""
                confirmButton.label: "Update"
                cancelButton.label: "Cancel"
                onFinished: {
                    if (value === SystemUiResult.ConfirmButtonSelection) {
                        var urlStr = aboutNav.pendingDownloadUrl;
                        var parts = urlStr.split("/");
                        var fname = parts[parts.length - 1] || ("Zalo10_" + aboutNav.pendingDownloadVersion + ".bar");
                        var qi = fname.indexOf("?");
                        if (qi >= 0) fname = fname.substring(0, qi);
                        // zService is a property passed in from main.qml
                        svcDownloadCon.target = aboutSheetRoot.zService;
                        updateProgressDialog.body = "Downloading Zalo10 " + aboutNav.pendingDownloadVersion + "…";
                        updateProgressDialog.progress = 0;
                        updateProgressDialog.show();
                        aboutSheetRoot.zService.downloadUpdate(urlStr, fname);
                    }
                }
            },
            // Progress dialog: shown while the BAR file is being downloaded.
            SystemProgressDialog {
                id: updateProgressDialog
                title: "Downloading Update"
                body: ""
                progress: 0
                cancelButton.label: "Cancel"
                onFinished: {
                    // If user taps Cancel mid-download, abort (just let it finish silently)
                    // QML has no direct abort hook here; the download will complete in bg
                    // but the file will be present in Downloads regardless.
                }
            }
        ]
    }
}
