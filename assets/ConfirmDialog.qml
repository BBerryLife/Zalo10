import bb.cascades 1.4
import bb.system 1.0

SystemDialog {
    property string confirmLabel: "OK"
    property string cancelLabel: "Cancel"
    signal confirmed()

    confirmButton.label: confirmLabel
    cancelButton.label: cancelLabel

    onFinished: {
        if (result === SystemUiResult.ConfirmButtonSelection)
            confirmed();
    }
}
