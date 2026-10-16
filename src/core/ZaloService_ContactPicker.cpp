#include "core/ZaloService.hpp"

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

// threadId travels with every result signal: several ChatView pages stay alive in the
// NavigationPane history and all listen to zService, so unfiltered results would be
// sent once per page.
void ZaloService::pickContact(const QString &threadId)
{
    if (m_contactPicker) {
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
    qWarning() << "[Picker] contact picker failed to open";
    QString tid = m_contactPickerThreadId;
    if (m_contactPicker) {
        m_contactPicker->deleteLater();
        m_contactPicker = 0;
    }
    m_contactPickerThreadId.clear();
    emit contactPickError(tid, "error");
}

// Escape VCF values per RFC 6350; newlines become a literal \n.
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
        qWarning() << "[Picker] invalid contact details, id=" << contactId;
        emit contactPickError(tid, "error");
        return;
    }

    QString displayName = c.displayName();
    if (displayName.trimmed().isEmpty()) displayName = "Contact";

    QString vcf;
    vcf += "BEGIN:VCARD\r\n";
    vcf += "VERSION:3.0\r\n";
    vcf += "FN:" + vcfEscape(displayName) + "\r\n";
    QString first = c.firstName();
    QString last  = c.lastName();
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
    }

    QString fileNameSafe = displayName;
    fileNameSafe.replace(QRegExp("[/\\\\:*?\"<>|]"), "_");
    QString destPath = "/tmp/zalo10_contact_" + fileNameSafe + "_"
                      + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".vcf";

    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[Picker] pickContact: cannot write" << destPath;
        emit contactPickError(tid, "error");
        return;
    }
    f.write(vcf.toUtf8());
    f.close();

    emit contactVcfReady(tid, destPath);
}
