import QtQuick 2.15
import WFVIEW 1.0

// Mouse-wheel adjustment for a Slider or Dial. Place it inside the control:
//
//     Slider { from: 0; to: 255; value: ...; onMoved: ...; WheelArea {} }
//
// Each notch moves about 1/stepsPerRange of the range, rounded to the
// control's stepSize, so wide-range controls don't need hundreds of notches.
// The value is changed through the control's own increase()/decrease(),
// which keeps any `value:` binding intact, and then moved() is emitted so
// the control's existing onMoved handler applies the change exactly as it
// would for a drag.
//
// For controls where stepping the value makes no sense (e.g. a wrapping
// tuning knob), set customHandler and handle wheeled() instead.
//
// Wheel events over an enabled control are consumed, so a surrounding
// ScrollView or panel doesn't scroll underneath it. The direction follows
// the Interface.ReverseWheel setting.
MouseArea {
    id: wheelArea

    property Item control: parent
    property bool customHandler: false
    property real stepsPerRange: 50
    readonly property bool reversed: {
        var v = MainController.settings ? MainController.settings.options["Interface.ReverseWheel"] : false
        return v === true || v === "true"
    }
    property real accumulated: 0

    // Emitted instead of stepping when customHandler is set. angleDelta is
    // already adjusted for the reverse setting (120 per standard notch).
    signal wheeled(int angleDelta, int modifiers)

    anchors.fill: parent
    acceptedButtons: Qt.NoButton

    function stepControl(notches) {
        var c = control
        var span = Math.abs(c.to - c.from)
        if (!(span > 0))
            return
        var step = span / stepsPerRange
        if (c.stepSize > 0)
            step = Math.max(c.stepSize, Math.round(step / c.stepSize) * c.stepSize)

        // increase()/decrease() move by stepSize; borrow it for one call so
        // the whole notch count is applied as a single value change.
        var savedStep = c.stepSize
        var before = c.value
        c.stepSize = step * Math.abs(notches)
        if (notches > 0)
            c.increase()
        else
            c.decrease()
        c.stepSize = savedStep
        if (Math.abs(c.value - before) > 1e-9)
            c.moved()
    }

    onWheel: function(wheel) {
        var d = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
        if (wheel.inverted)
            d = -d
        if (wheelArea.reversed)
            d = -d
        wheel.accepted = true

        if (wheelArea.customHandler) {
            if (d !== 0)
                wheelArea.wheeled(d, wheel.modifiers)
            return
        }

        // Accumulate high-resolution (touchpad) deltas into whole notches.
        accumulated += d
        var notches = accumulated > 0 ? Math.floor(accumulated / 120) : Math.ceil(accumulated / 120)
        if (notches === 0)
            return
        accumulated -= notches * 120
        stepControl(notches)
    }
}
