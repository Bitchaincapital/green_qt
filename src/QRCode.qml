import QtQuick 2.3

Image {
    id: self

    property string text

    fillMode: Image.PreserveAspectFit
    horizontalAlignment: Image.AlignHCenter
    verticalAlignment: Image.AlignVCenter
    smooth: false
    mipmap: false
    cache: false

    sourceSize.width: 320
    sourceSize.height: 320

    states: State {
        when: text.length > 0
        PropertyChanges {
            target: self
            source: `image://QZXing/encode/${text}?format=qrcode&border=true&transparent=true`
        }
    }
}
