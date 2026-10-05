// ZaloService khai báo pickContact() và 3 slot ContactPicker (Q_INVOKABLE/slot nên
// moc sinh tham chiếu tới chúng). Trong service headless không có UI để mở picker,
// nên thay bản thật (ZaloService_ContactPicker.cpp, cần bb::cascades) bằng stub rỗng
// để khỏi link libbbcascades vào process nền.
#include "ZaloService.hpp"

void ZaloService::pickContact(const QString &) {}
void ZaloService::onContactPickerCanceled() {}
void ZaloService::onContactPickerError() {}
void ZaloService::onContactPickerContactSelected(int) {}
