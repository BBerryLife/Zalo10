#include "ZaloService.hpp"

#include <bb/cascades/pickers/ContactPicker>
#include <bb/cascades/pickers/ContactSelectionMode>
#include <bb/pim/contacts/ContactService>
#include <bb/pim/contacts/Contact>
#include <bb/pim/contacts/ContactAttribute>

#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <QRegExp>

using namespace bb::cascades::pickers;
using namespace bb::pim::contacts;

// ─── pickContact: mở ContactPicker (single-select), build .vcf từ contact
// được chọn, lưu ra /tmp, rồi emit contactVcfReady(threadId, path) ─────────
// ContactPicker (bb::cascades::pickers) là 1 full-screen picker riêng của
// nó, không phải QML type như FilePicker/AudioRecorder — mở bằng open() và
// nhận kết quả qua signal, y hệt cách các *Reply* HTTP bất đồng bộ khác
// trong ZaloService xử lý (lưu context, chờ signal, dọn dẹp trong slot).
//
// threadId lưu vào m_contactPickerThreadId và mang theo lại trong mọi
// signal kết quả — bắt buộc, xem ghi chú dài ở khai báo pickContact() trong
// ZaloService.hpp để hiểu lý do (nhiều ChatView Page cùng sống trong
// NavigationPane history, tất cả cùng lắng nghe zService — thiếu threadId
// để lọc thì mọi Page đều tự gửi, gây trùng file).
void ZaloService::pickContact(const QString &threadId)
{
    if (m_contactPicker) {
        // Đang có 1 picker khác mở dở (không nên xảy ra — QML chỉ gọi
        // pickContact() sau khi sheet trước đã đóng) — dọn cái cũ trước để
        // tránh rò rỉ, rồi tiếp tục mở cái mới.
        m_contactPicker->deleteLater();
        m_contactPicker = 0;
    }

    m_contactPickerThreadId = threadId;
    m_contactPicker = new ContactPicker(this);
    m_contactPicker->setMode(ContactSelectionMode::Single);
    m_contactPicker->setTitle("Select Contact");

    connect(m_contactPicker, SIGNAL(contactSelected(int)),
            this, SLOT(onContactPickerContactSelected(int)));
    connect(m_contactPicker, SIGNAL(canceled()),
            this, SLOT(onContactPickerCanceled()));
    connect(m_contactPicker, SIGNAL(error()),
            this, SLOT(onContactPickerError()));

    m_contactPicker->open();
}

void ZaloService::onContactPickerCanceled()
{
    QString tid = m_contactPickerThreadId;
    if (m_contactPicker) {
        m_contactPicker->deleteLater();
        m_contactPicker = 0;
    }
    m_contactPickerThreadId.clear();
    emit contactPickError(tid, "canceled");
}

void ZaloService::onContactPickerError()
{
    qDebug() << "[Zalo] pickContact: ContactPicker::error() — picker failed to open (system resources depleted?)";
    QString tid = m_contactPickerThreadId;
    if (m_contactPicker) {
        m_contactPicker->deleteLater();
        m_contactPicker = 0;
    }
    m_contactPickerThreadId.clear();
    emit contactPickError(tid, "error");
}

// Escape ký tự đặc biệt trong giá trị field VCF theo RFC 6350 (VCF 3.0):
// backslash, dấu phẩy, chấm phẩy phải escape; xuống dòng thành "\n" literal
// (2 ký tự \ và n, không phải newline thật — 1 field VCF luôn nằm trên 1
// dòng logic).
static QString vcfEscape(const QString &s)
{
    QString out = s;
    out.replace("\\", "\\\\");
    out.replace(",", "\\,");
    out.replace(";", "\\;");
    out.replace("\n", "\\n");
    return out;
}

void ZaloService::onContactPickerContactSelected(int contactId)
{
    QString tid = m_contactPickerThreadId;
    if (m_contactPicker) {
        m_contactPicker->deleteLater();
        m_contactPicker = 0;
    }
    m_contactPickerThreadId.clear();

    ContactService svc;
    Contact c = svc.contactDetails(contactId);
    if (!c.isValid()) {
        qDebug() << "[Zalo] pickContact: contactDetails invalid for id" << contactId;
        emit contactPickError(tid, "error");
        return;
    }

    QString displayName = c.displayName();
    if (displayName.trimmed().isEmpty()) displayName = "Contact";

    // VCF 3.0 tối giản: FN + N (bắt buộc theo spec) + TEL/EMAIL lặp lại cho
    // mỗi số/địa chỉ contact có. Không phân loại Home/Work/Mobile — Zalo
    // trên các nền tảng khác cũng thường chỉ hiện phẳng danh sách số/email
    // khi nhận 1 file .vcf, nên bỏ qua subKind cho đơn giản, đủ dùng.
    QString vcf;
    vcf += "BEGIN:VCARD\r\n";
    vcf += "VERSION:3.0\r\n";
    vcf += "FN:" + vcfEscape(displayName) + "\r\n";
    QString first = c.firstName();
    QString last  = c.lastName();
    // N:Last;First;;; — thứ tự bắt buộc theo spec dù nhiều field để trống
    vcf += "N:" + vcfEscape(last) + ";" + vcfEscape(first) + ";;;\r\n";

    QList<ContactAttribute> phones = c.phoneNumbers();
    for (int i = 0; i < phones.size(); ++i) {
        QString v = phones.at(i).value();
        if (v.trimmed().isEmpty()) continue;
        vcf += "TEL:" + vcfEscape(v) + "\r\n";
    }
    QList<ContactAttribute> emails = c.emails();
    for (int i = 0; i < emails.size(); ++i) {
        QString v = emails.at(i).value();
        if (v.trimmed().isEmpty()) continue;
        vcf += "EMAIL:" + vcfEscape(v) + "\r\n";
    }
    vcf += "END:VCARD\r\n";

    if (phones.isEmpty() && emails.isEmpty()) {
        // Contact có tên nhưng không số/không email — vẫn hợp lệ về mặt
        // VCF (FN/N là đủ), nhưng gần như vô nghĩa khi gửi cho người khác.
        // Không chặn cứng (Zalo cho gửi vẫn hơn tự ý từ chối), chỉ log để
        // dễ debug nếu Jim báo "gửi contact rỗng".
        qDebug() << "[Zalo] pickContact: contact" << displayName << "has no phone/email, vcf will be name-only";
    }

    QString fileNameSafe = displayName;
    fileNameSafe.replace(QRegExp("[/\\\\:*?\"<>|]"), "_");
    QString destPath = "/tmp/zalo10_contact_" + fileNameSafe + "_"
                      + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".vcf";

    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qDebug() << "[Zalo] pickContact: cannot write" << destPath;
        emit contactPickError(tid, "error");
        return;
    }
    f.write(vcf.toUtf8());
    f.close();

    qDebug() << "[Zalo] pickContact: built vcf for" << displayName << "->" << destPath;
    emit contactVcfReady(tid, destPath);
}
