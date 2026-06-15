import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: loginView
    signal loginSuccessful()

    titleBar: TitleBar {
        title: "Sign in to Zalo10"
        kind: TitleBarKind.Default
        appearance: TitleBarAppearance.Branded
    }

    onCreationCompleted: {
        if (!zService.qrReady) {
            zService.startQRLogin();
        }
    }

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill

        Container { preferredHeight: ui.du(3) }

        Label {
            text: "Scan the QR code with the Zalo app on your phone"
            multiline: true
            horizontalAlignment: HorizontalAlignment.Center
            textStyle {
                base:      SystemDefaults.TextStyles.BodyText
                textAlign: TextAlign.Center
            }
        }

        Container { preferredHeight: ui.du(2) }

        Container {
            horizontalAlignment: HorizontalAlignment.Center
            layout: DockLayout {}

            Container {
                background: Color.White
                preferredWidth:  ui.du(44)
                preferredHeight: ui.du(44)
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment:   VerticalAlignment.Center
                layout: DockLayout {}

                ImageView {
                    id: qrImage
                    preferredWidth:  ui.du(42)
                    preferredHeight: ui.du(42)
                    scalingMethod: ScalingMethod.AspectFit
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment:   VerticalAlignment.Center
                    visible: false
                }

                ActivityIndicator {
                    id: qrLoading
                    preferredWidth:  ui.du(14)
                    preferredHeight: ui.du(14)
                    horizontalAlignment: HorizontalAlignment.Center
                    verticalAlignment:   VerticalAlignment.Center
                    running: visible
                    visible: true
                }

                Container {
                    id: expiredOverlay
                    visible: false
                    background: Color.create("#CC000000")
                    horizontalAlignment: HorizontalAlignment.Fill
                    verticalAlignment:   VerticalAlignment.Fill
                    layout: DockLayout {}
                    Label {
                        text: "QR Expired"
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment:   VerticalAlignment.Center
                        textStyle { base: SystemDefaults.TextStyles.TitleText; color: Color.White }
                    }
                }
            }
        }

        Container { preferredHeight: ui.du(2) }

        Label {
            id: statusLabel
            text: "Loading QR code..."
            horizontalAlignment: HorizontalAlignment.Center
            textStyle {
                base:      SystemDefaults.TextStyles.BodyText
                textAlign: TextAlign.Center
            }
        }

        Container { preferredHeight: ui.du(2) }

        Button {
            id: retryBtn
            text: "Refresh QR"
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            onClicked: {
                expiredOverlay.visible = false;
                retryBtn.visible  = false;
                qrImage.visible   = false;
                qrLoading.visible = true;
                statusLabel.text  = "Loading QR code...";
                zService.retryQRLogin();
            }
        }
    }

    attachedObjects: [
        Connections {
            target: zService
            onQrCodeReady: {
                qrLoading.visible      = false;
                expiredOverlay.visible = false;
                retryBtn.visible       = false;
                if (imagePath.indexOf("data:") === 0) {
                    statusLabel.text = "Code: " + qrCode.substring(0, 16) + "...";
                } else {
                    qrImage.imageSource = imagePath;
                    qrImage.visible     = true;
                    statusLabel.text    = "Scan with Zalo on your phone";
                }
            }
            onQrScanned: {
                statusLabel.text  = "Scanned! Confirming...";
                qrImage.visible   = false;
                qrLoading.visible = true;
            }
            onQrExpired: {
                qrLoading.visible      = false;
                expiredOverlay.visible = true;
                retryBtn.visible       = true;
                statusLabel.text       = "QR code has expired";
            }
            onLoginSuccess: { loginView.loginSuccessful(); }
            onLoginFailed:  {
                statusLabel.text  = "Error: " + message;
                retryBtn.visible  = true;
                qrLoading.visible = false;
            }
        }
    ]
}
