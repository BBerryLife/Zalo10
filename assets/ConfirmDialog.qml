import bb.cascades 1.4
import bb.system 1.0

// Reusable Confirm/Cancel system dialog. Set title/body/confirmLabel and
// handle onConfirmed to perform the actual action — keeps the dialog itself
// free of any business logic.
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
