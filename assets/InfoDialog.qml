import bb.cascades 1.4
import bb.system 1.0

// OK-only informational dialog (no cancel button) — e.g. for "feature not
// implemented yet" notices. Set title/body like a normal SystemDialog.
SystemDialog {
    confirmButton.label: "OK"
    cancelButton.label: ""
    cancelButton.enabled: false
}
