// LoginView.qml
import bb.cascades 1.4
import QtQuick 1.0

Page {
    id: loginView
    signal loginSuccessful()

    // Native TitleBar với màu gradient #70cbff
    titleBar: TitleBar {
        title: "Đăng nhập Zalo10"
        kind: TitleBarKind.Default
        appearance: TitleBarAppearance.Branded
    }

    onCreationCompleted: {
        zService.startQRLogin();
    }

    content: Container {
        layout: StackLayout { orientation: LayoutOrientation.TopToBottom }
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment:   VerticalAlignment.Fill
        // Background mặc định hệ thống

        Container { preferredHeight: ui.du(3) }

        Label {
            text: "Quét mã QR bằng ứng dụng Zalo trên điện thoại"
            multiline: true
            horizontalAlignment: HorizontalAlignment.Center
            textStyle {
                base:      SystemDefaults.TextStyles.BodyText
                textAlign: TextAlign.Center
            }
        }

        Container { preferredHeight: ui.du(2) }

        // - Khung QR lớn hơn -
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
                        text: "QR hết hạn"
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
            text: "Đang tải mã QR..."
            horizontalAlignment: HorizontalAlignment.Center
            textStyle {
                base:      SystemDefaults.TextStyles.BodyText
                textAlign: TextAlign.Center
            }
        }

        Container { preferredHeight: ui.du(2) }

        Button {
            id: retryBtn
            text: "Làm mới QR"
            visible: false
            horizontalAlignment: HorizontalAlignment.Center
            onClicked: {
                expiredOverlay.visible = false;
                retryBtn.visible  = false;
                qrImage.visible   = false;
                qrLoading.visible = true;
                statusLabel.text  = "Đang tải mã QR...";
                zService.retryQRLogin();
            }
        }
    }

    attachedObjects: [
        Connections {
            target: zService
            onQrCodeReady: {
                qrLoading.visible  = false;
                expiredOverlay.visible = false;
                retryBtn.visible   = false;
                if (imagePath.indexOf("data:") === 0) {
                    statusLabel.text = "Mã: " + qrCode.substring(0, 16) + "...";
                } else {
                    qrImage.imageSource = imagePath;
                    qrImage.visible     = true;
                    statusLabel.text    = "Quét bằng Zalo trên điện thoại";
                }
            }
            onQrScanned: {
                statusLabel.text  = "Đã quét! Đang xác nhận...";
                qrImage.visible   = false;
                qrLoading.visible = true;
            }
            onQrExpired: {
                qrLoading.visible  = false;
                expiredOverlay.visible = true;
                retryBtn.visible   = true;
                statusLabel.text   = "Mã QR đã hết hạn";
            }
            onLoginSuccess: { loginView.loginSuccessful(); }
            onLoginFailed:  {
                statusLabel.text = "Lỗi: " + message;
                retryBtn.visible = true;
                qrLoading.visible = false;
            }
        }
    ]
}
