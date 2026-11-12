import QtQuick 2.15
import QtQuick.Controls 2.15

// Continuous 360° tuning knob.
//
// Dial maps its value onto a fixed 280° arc, leaving a dead zone at the
// bottom that the handle jumps across when it wraps. Here Dial supplies only
// the background; the handle angle and the drag input are handled locally so
// the knob turns freely through any angle.
//
// Dragging emits stepped() once per stepDegrees of rotation (with the same
// fast-spin acceleration the old dial had). The wheel emits wheeled() with
// the raw angleDelta so the receiver can accumulate it, and turns the knob
// stepDegrees per standard notch.
Dial {
    id: knob

    property real stepDegrees: 3.6        // 100 steps per revolution
    property real knobAngle: 0            // handle angle, 0 = top, clockwise

    signal stepped(int steps)
    signal wheeled(int angleDelta, int modifiers)

    property real dragAccumulated: 0
    property real lastPointerAngle: 0
    property real lastKeyValue: value
    property double lastStepTime: 0
    property int lastDirection: 0
    property int fastMoveCount: 0

    from: 0
    to: 1000
    stepSize: 10
    wrap: true

    function rotateBy(degrees) {
        knobAngle = ((knobAngle + degrees) % 360 + 360) % 360
    }

    // Faster spinning tunes in bigger jumps, as the old dial did.
    function accelerated(direction) {
        const now = Date.now()
        const elapsed = lastStepTime > 0 ? Math.max(1, now - lastStepTime) : 0
        var multiplier = 1

        if (direction === lastDirection && elapsed > 0 && elapsed <= 90)
            fastMoveCount += 1
        else
            fastMoveCount = 0

        if (fastMoveCount >= 5 && elapsed <= 35)
            multiplier = 10
        else if (fastMoveCount >= 3 && elapsed <= 60)
            multiplier = 5
        else if (fastMoveCount >= 2 && elapsed <= 90)
            multiplier = 2

        lastDirection = direction
        lastStepTime = now
        return multiplier
    }

    handle: Rectangle {
        x: knob.background.x + knob.background.width / 2 - width / 2
        y: knob.background.y + knob.background.height / 2 - height / 2
        width: knob.width / 7
        height: knob.height / 7
        radius: width / 2
        border.color: Qt.darker(knob.palette.button, 1.6)
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(knob.palette.button, 1.15) }
            GradientStop { position: 1.0; color: Qt.darker(knob.palette.button, 1.1) }
        }
        transform: [
            Translate {
                y: -Math.min(knob.background.width, knob.background.height) * 0.42 + knob.handle.height
            },
            Rotation {
                angle: knob.knobAngle
                origin.x: knob.handle.width / 2
                origin.y: knob.handle.height / 2
            }
        ]
    }

    // Keyboard steps still arrive through Dial's own value.
    onMoved: {
        const span = to - from
        var delta = value - lastKeyValue
        if (delta > span / 2)
            delta -= span
        else if (delta < -span / 2)
            delta += span
        lastKeyValue = value
        if (delta === 0)
            return
        const direction = delta > 0 ? 1 : -1
        rotateBy(direction * stepDegrees)
        stepped(direction)
    }

    WheelArea {
        customHandler: true
        onWheeled: function(angleDelta, modifiers) {
            knob.rotateBy(knob.stepDegrees * angleDelta / 120)
            knob.wheeled(angleDelta, modifiers)
        }
    }

    // Sits above Dial's own input, so Dial never sees the press.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        preventStealing: true

        // Pointer angle in degrees, 0 at the top, clockwise, range -180..180.
        function pointerAngle(x, y) {
            return Math.atan2(x - width / 2, height / 2 - y) * 180 / Math.PI
        }

        // Too close to the centre the angle swings wildly; ignore it there.
        function nearCentre(x, y) {
            return Math.hypot(x - width / 2, y - height / 2) < Math.min(width, height) * 0.1
        }

        onPressed: function(mouse) {
            knob.forceActiveFocus()
            knob.lastPointerAngle = pointerAngle(mouse.x, mouse.y)
            knob.dragAccumulated = 0
        }

        onPositionChanged: function(mouse) {
            const a = pointerAngle(mouse.x, mouse.y)
            var d = a - knob.lastPointerAngle
            knob.lastPointerAngle = a
            if (nearCentre(mouse.x, mouse.y))
                return

            // Crossing the ±180° seam at the bottom: take the short way round.
            if (d > 180)
                d -= 360
            else if (d < -180)
                d += 360

            knob.rotateBy(d)

            knob.dragAccumulated += d
            const steps = Math.trunc(knob.dragAccumulated / knob.stepDegrees)
            if (steps === 0)
                return
            knob.dragAccumulated -= steps * knob.stepDegrees
            const direction = steps > 0 ? 1 : -1
            knob.stepped(steps * knob.accelerated(direction))
        }
    }
}
