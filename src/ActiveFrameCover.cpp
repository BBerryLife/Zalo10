#include "ActiveFrameCover.hpp"

#include <bb/cascades/Container>
#include <bb/cascades/DockLayout>
#include <bb/cascades/HorizontalAlignment>
#include <bb/cascades/VerticalAlignment>
#include <bb/cascades/ScalingMethod>
#include <bb/device/DisplayInfo>

#include <QDebug>

using namespace bb::cascades;
using namespace bb::device;

ActiveFrameCover::ActiveFrameCover()
{
    DisplayInfo display;
    QSize px = display.pixelSize();
    int w = px.width(), h = px.height();
    if (w > h) { int t = w; w = h; h = t; }

    // Pick image path based on screen resolution
    QString imgPath;
    if (w >= 1440)
        imgPath = "asset:///images/ActiveFrame/activeframe_zl10_big.png";
    else if (h >= 1280)
        imgPath = "asset:///images/ActiveFrame/activeframe_zl10_big.png";
    else
        imgPath = "asset:///images/ActiveFrame/activeframe_zl10_medium.png";

    qDebug() << "[ActiveFrame] screen" << w << "x" << h << "-> img:" << imgPath;

    Container *root = Container::create()
        .layout(new DockLayout())
        .horizontal(HorizontalAlignment::Fill)
        .vertical(VerticalAlignment::Fill);

    ImageView *img = ImageView::create()
        .horizontal(HorizontalAlignment::Fill)
        .vertical(VerticalAlignment::Fill)
        .scalingMethod(ScalingMethod::AspectFill);

    // Use setImageSource(QString) — same pattern as bbtube's showDefaultImage()
    img->setImageSource(imgPath);

    root->add(img);
    this->setContent(root);
}
