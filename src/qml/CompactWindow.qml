import QtQuick
import QtQuick.Window

Window {
    signal openMain()
    function toggle() { visible = !visible }
}
