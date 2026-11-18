import bb.cascades 1.4

// Invisible 1-pixel focus target. Cascades puts the trackpad highlight on the first focusable control
// when a page opens, which looks ugly on a header button. This control takes that initial focus
// instead (locallyFocused) and draws no highlight, so nothing lights up until the user moves the
// trackpad (focus then moves to the nearest real control) or touches the screen.
Container {
    preferredWidth: 1
    preferredHeight: 1
    locallyFocused: true
    navigation.focusPolicy: NavigationFocusPolicy.Focusable
    navigation.defaultHighlightEnabled: false
}
