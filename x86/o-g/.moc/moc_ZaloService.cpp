/****************************************************************************
** Meta object code from reading C++ file 'ZaloService.hpp'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ZaloService.hpp"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ZaloService.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_ZaloService[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      64,   14, // methods
       1,  334, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      14,       // signalCount

 // signals: signature, parameters, type, tag, flags
      13,   12,   12,   12, 0x05,
      39,   31,   12,   12, 0x05,
      76,   60,   12,   12, 0x05,
     123,  106,   12,   12, 0x05,
     164,  152,   12,   12, 0x05,
     183,   12,   12,   12, 0x05,
     203,  195,   12,   12, 0x05,
     244,  236,   12,   12, 0x05,
     279,  271,   12,   12, 0x05,
     324,  306,   12,   12, 0x05,
     377,  360,   12,   12, 0x05,
     420,  403,   12,   12, 0x05,
     478,  452,   12,   12, 0x05,
     547,  528,   12,   12, 0x05,

 // slots: signature, parameters, type, tag, flags
     576,   12,   12,   12, 0x08,
     590,   12,   12,   12, 0x08,
     604,   12,   12,   12, 0x08,
     618,   12,   12,   12, 0x08,
     632,   12,   12,   12, 0x08,
     646,   12,   12,   12, 0x08,
     665,   12,   12,   12, 0x08,
     679,   12,   12,   12, 0x08,
     693,   12,   12,   12, 0x08,
     707,   12,   12,   12, 0x08,
     721,   12,   12,   12, 0x08,
     741,   12,   12,   12, 0x08,
     761,   12,   12,   12, 0x08,
     780,   12,   12,   12, 0x08,
     801,   12,   12,   12, 0x08,
     822,   12,   12,   12, 0x08,
     843,   12,   12,   12, 0x08,
     860,   12,   12,   12, 0x08,
     876,   12,   12,   12, 0x08,
     890,   12,   12,   12, 0x08,
     906,   12,   12,   12, 0x08,
     921,   12,   12,   12, 0x08,
     937,   12,   12,   12, 0x08,
     958,   12,   12,   12, 0x08,
     974,   12,   12,   12, 0x08,
     990,   12,   12,   12, 0x08,
    1006,   12,   12,   12, 0x08,
    1032, 1025,   12,   12, 0x08,
    1064,   12,   12,   12, 0x08,

 // methods: signature, parameters, type, tag, flags
    1085,   12,   12,   12, 0x02,
    1100,   12,   12,   12, 0x02,
    1115,   12,   12,   12, 0x02,
    1131,   12,   12,   12, 0x02,
    1167, 1140,   12,   12, 0x02,
    1245, 1224,   12,   12, 0x22,
    1312, 1294,   12,   12, 0x22,
    1366, 1353,   12,   12, 0x22,
    1404,   12, 1399,   12, 0x02,
    1418,   12,   12,   12, 0x02,
    1432,   12,   12,   12, 0x02,
    1453,   12,   12,   12, 0x02,
    1468,   12,   12,   12, 0x02,
    1492, 1483,   12,   12, 0x02,
    1540, 1523,   12,   12, 0x02,
    1593, 1568,   12,   12, 0x02,
    1640, 1627,   12,   12, 0x02,
    1672, 1523,   12,   12, 0x02,
    1702,   12,   12,   12, 0x02,
    1735, 1722,   12,   12, 0x02,
    1792, 1783, 1770,   12, 0x02,

 // properties: name, type, flags
    1816, 1399, 0x01495001,

 // properties: notify_signal_id
       0,

       0        // eod
};

static const char qt_meta_stringdata_ZaloService[] = {
    "ZaloService\0\0loggedInChanged()\0message\0"
    "loginFailed(QString)\0uid,displayName\0"
    "loginSuccess(QString,QString)\0"
    "imagePath,qrCode\0qrCodeReady(QString,QString)\0"
    "displayName\0qrScanned(QString)\0"
    "qrExpired()\0threads\0"
    "conversationsReady(QVariantList)\0"
    "friends\0friendsReady(QVariantList)\0"
    "invites\0invitesReady(QVariantList)\0"
    "threadId,messages\0messagesReady(QString,QVariantList)\0"
    "success,threadId\0messageSent(bool,QString)\0"
    "threadId,message\0newMessage(QString,QVariantMap)\0"
    "threadId,lastMsg,lastTime\0"
    "threadLastMessageChanged(QString,QString,QString)\0"
    "threadId,localPath\0avatarReady(QString,QString)\0"
    "onStep1Done()\0onStep2Done()\0onStep3Done()\0"
    "onStep4Done()\0onStep5Done()\0"
    "onQRImageFetched()\0onStep6Done()\0"
    "onStep7Done()\0onStep8Done()\0onStep9Done()\0"
    "onCookieStep1Done()\0onCookieStep2Done()\0"
    "onFetchConvoDone()\0onFetchFriendsDone()\0"
    "onFetchInvitesDone()\0onGroupDetailsDone()\0"
    "onFetchMsgDone()\0onSendMsgDone()\0"
    "onQRExpired()\0onListenTimer()\0"
    "onListenDone()\0onPollMsgDone()\0"
    "onAvatarDownloaded()\0onWsConnected()\0"
    "onWsEncrypted()\0onWsReadyRead()\0"
    "onWsDisconnected()\0errors\0"
    "onWsSslErrors(QList<QSslError>)\0"
    "onWsReconnectTimer()\0startQRLogin()\0"
    "retryQRLogin()\0cancelQRLogin()\0logout()\0"
    "zpsid,zpwSek,imei,ua,token\0"
    "loginWithCookie(QString,QString,QString,QString,QString)\0"
    "zpsid,zpwSek,imei,ua\0"
    "loginWithCookie(QString,QString,QString,QString)\0"
    "zpsid,zpwSek,imei\0"
    "loginWithCookie(QString,QString,QString)\0"
    "zpsid,zpwSek\0loginWithCookie(QString,QString)\0"
    "bool\0loadSession()\0saveSession()\0"
    "fetchConversations()\0fetchFriends()\0"
    "fetchInvites()\0groupIds\0"
    "fetchGroupDetails(QStringList)\0"
    "threadId,isGroup\0fetchMessages(QString,bool)\0"
    "threadId,content,isGroup\0"
    "sendMessage(QString,QString,bool)\0"
    "threadId,url\0downloadAvatar(QString,QString)\0"
    "setActiveThread(QString,bool)\0"
    "clearActiveThread()\0msg,threadId\0"
    "dbSaveMessage(QVariantMap,QString)\0"
    "QVariantList\0threadId\0dbLoadMessages(QString)\0"
    "loggedIn\0"
};

void ZaloService::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        ZaloService *_t = static_cast<ZaloService *>(_o);
        switch (_id) {
        case 0: _t->loggedInChanged(); break;
        case 1: _t->loginFailed((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->loginSuccess((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 3: _t->qrCodeReady((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 4: _t->qrScanned((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 5: _t->qrExpired(); break;
        case 6: _t->conversationsReady((*reinterpret_cast< const QVariantList(*)>(_a[1]))); break;
        case 7: _t->friendsReady((*reinterpret_cast< const QVariantList(*)>(_a[1]))); break;
        case 8: _t->invitesReady((*reinterpret_cast< const QVariantList(*)>(_a[1]))); break;
        case 9: _t->messagesReady((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QVariantList(*)>(_a[2]))); break;
        case 10: _t->messageSent((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 11: _t->newMessage((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QVariantMap(*)>(_a[2]))); break;
        case 12: _t->threadLastMessageChanged((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 13: _t->avatarReady((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 14: _t->onStep1Done(); break;
        case 15: _t->onStep2Done(); break;
        case 16: _t->onStep3Done(); break;
        case 17: _t->onStep4Done(); break;
        case 18: _t->onStep5Done(); break;
        case 19: _t->onQRImageFetched(); break;
        case 20: _t->onStep6Done(); break;
        case 21: _t->onStep7Done(); break;
        case 22: _t->onStep8Done(); break;
        case 23: _t->onStep9Done(); break;
        case 24: _t->onCookieStep1Done(); break;
        case 25: _t->onCookieStep2Done(); break;
        case 26: _t->onFetchConvoDone(); break;
        case 27: _t->onFetchFriendsDone(); break;
        case 28: _t->onFetchInvitesDone(); break;
        case 29: _t->onGroupDetailsDone(); break;
        case 30: _t->onFetchMsgDone(); break;
        case 31: _t->onSendMsgDone(); break;
        case 32: _t->onQRExpired(); break;
        case 33: _t->onListenTimer(); break;
        case 34: _t->onListenDone(); break;
        case 35: _t->onPollMsgDone(); break;
        case 36: _t->onAvatarDownloaded(); break;
        case 37: _t->onWsConnected(); break;
        case 38: _t->onWsEncrypted(); break;
        case 39: _t->onWsReadyRead(); break;
        case 40: _t->onWsDisconnected(); break;
        case 41: _t->onWsSslErrors((*reinterpret_cast< const QList<QSslError>(*)>(_a[1]))); break;
        case 42: _t->onWsReconnectTimer(); break;
        case 43: _t->startQRLogin(); break;
        case 44: _t->retryQRLogin(); break;
        case 45: _t->cancelQRLogin(); break;
        case 46: _t->logout(); break;
        case 47: _t->loginWithCookie((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4])),(*reinterpret_cast< const QString(*)>(_a[5]))); break;
        case 48: _t->loginWithCookie((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4]))); break;
        case 49: _t->loginWithCookie((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 50: _t->loginWithCookie((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 51: { bool _r = _t->loadSession();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = _r; }  break;
        case 52: _t->saveSession(); break;
        case 53: _t->fetchConversations(); break;
        case 54: _t->fetchFriends(); break;
        case 55: _t->fetchInvites(); break;
        case 56: _t->fetchGroupDetails((*reinterpret_cast< const QStringList(*)>(_a[1]))); break;
        case 57: _t->fetchMessages((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 58: _t->sendMessage((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< bool(*)>(_a[3]))); break;
        case 59: _t->downloadAvatar((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 60: _t->setActiveThread((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 61: _t->clearActiveThread(); break;
        case 62: _t->dbSaveMessage((*reinterpret_cast< const QVariantMap(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 63: { QVariantList _r = _t->dbLoadMessages((*reinterpret_cast< const QString(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< QVariantList*>(_a[0]) = _r; }  break;
        default: ;
        }
    }
}

const QMetaObjectExtraData ZaloService::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject ZaloService::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_ZaloService,
      qt_meta_data_ZaloService, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &ZaloService::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *ZaloService::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *ZaloService::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_ZaloService))
        return static_cast<void*>(const_cast< ZaloService*>(this));
    return QObject::qt_metacast(_clname);
}

int ZaloService::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 64)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 64;
    }
#ifndef QT_NO_PROPERTIES
      else if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< bool*>(_v) = loggedIn(); break;
        }
        _id -= 1;
    } else if (_c == QMetaObject::WriteProperty) {
        _id -= 1;
    } else if (_c == QMetaObject::ResetProperty) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyDesignable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyScriptable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyStored) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyEditable) {
        _id -= 1;
    } else if (_c == QMetaObject::QueryPropertyUser) {
        _id -= 1;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void ZaloService::loggedInChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, 0);
}

// SIGNAL 1
void ZaloService::loginFailed(const QString & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ZaloService::loginSuccess(const QString & _t1, const QString & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ZaloService::qrCodeReady(const QString & _t1, const QString & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void ZaloService::qrScanned(const QString & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void ZaloService::qrExpired()
{
    QMetaObject::activate(this, &staticMetaObject, 5, 0);
}

// SIGNAL 6
void ZaloService::conversationsReady(const QVariantList & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void ZaloService::friendsReady(const QVariantList & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void ZaloService::invitesReady(const QVariantList & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void ZaloService::messagesReady(const QString & _t1, const QVariantList & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}

// SIGNAL 10
void ZaloService::messageSent(bool _t1, const QString & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 10, _a);
}

// SIGNAL 11
void ZaloService::newMessage(const QString & _t1, const QVariantMap & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 11, _a);
}

// SIGNAL 12
void ZaloService::threadLastMessageChanged(const QString & _t1, const QString & _t2, const QString & _t3)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 12, _a);
}

// SIGNAL 13
void ZaloService::avatarReady(const QString & _t1, const QString & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 13, _a);
}
QT_END_MOC_NAMESPACE
