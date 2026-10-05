/****************************************************************************
** Meta object code from reading C++ file 'applicationui.hpp'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/applicationui.hpp"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'applicationui.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_ApplicationUI[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      36,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: signature, parameters, type, tag, flags
      32,   15,   14,   14, 0x05,
      71,   66,   14,   14, 0x05,
     110,  105,   14,   14, 0x05,
     174,  133,   14,   14, 0x05,
     233,  222,   14,   14, 0x05,
     273,  265,   14,   14, 0x05,
     319,  305,   14,   14, 0x05,

 // slots: signature, parameters, type, tag, flags
     346,   14,   14,   14, 0x0a,
     381,  370,   14,   14, 0x0a,
     410,   14,   14,   14, 0x0a,
     424,  105,   14,   14, 0x0a,
     448,   14,  443,   14, 0x0a,
     463,   66,   14,   14, 0x0a,
     493,   14,  443,   14, 0x0a,
     527,   14,  519,   14, 0x0a,
     540,   14,  519,   14, 0x0a,
     552,   14,   14,   14, 0x0a,
     569,   14,   14,   14, 0x0a,
     591,  586,   14,   14, 0x0a,
     626,  616,  443,   14, 0x0a,
     656,  586,   14,   14, 0x0a,
     702,  683,   14,   14, 0x0a,
     745,  616,   14,   14, 0x0a,
     804,  780,   14,   14, 0x0a,
     855,  616,   14,   14, 0x0a,
     907,  878,   14,   14, 0x0a,
     945,   14,   14,   14, 0x08,
     971,   14,   14,   14, 0x08,
     989,  986,   14,   14, 0x08,
    1015, 1007,   14,   14, 0x08,
    1052,   14,   14,   14, 0x08,
    1070,   14,   14,   14, 0x08,
    1091,   14,   14,   14, 0x08,
    1116,   14,   14,   14, 0x08,
    1146, 1139,   14,   14, 0x08,
    1184,   14,   14,   14, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_ApplicationUI[] = {
    "ApplicationUI\0\0threadId,isGroup\0"
    "openThreadRequested(QString,bool)\0"
    "show\0showRecalledMessagesChanged(bool)\0"
    "dark\0darkThemeChanged(bool)\0"
    "isLatest,latestVersion,downloadUrl,error\0"
    "updateCheckResult(bool,QString,QString,QString)\0"
    "html,error\0changelogReady(QString,QString)\0"
    "targets\0shareTargetsReady(QVariantList)\0"
    "success,error\0eventCreated(bool,QString)\0"
    "onServicePingFinished()\0to,subject\0"
    "invokeEmail(QString,QString)\0minimizeApp()\0"
    "setDarkTheme(bool)\0bool\0getDarkTheme()\0"
    "setShowRecalledMessages(bool)\0"
    "getShowRecalledMessages()\0QString\0"
    "appVersion()\0exportLog()\0checkForUpdate()\0"
    "fetchChangelog()\0text\0copyToClipboard(QString)\0"
    "localPath\0copyImageToClipboard(QString)\0"
    "queryShareTargets(QString)\0"
    "target,action,text\0"
    "invokeShareTarget(QString,QString,QString)\0"
    "queryShareTargetsForImage(QString)\0"
    "target,action,localPath\0"
    "invokeShareTargetForImage(QString,QString,QString)\0"
    "openLocalFile(QString)\0"
    "subject,body,durationMinutes\0"
    "createTodayEvent(QString,QString,int)\0"
    "onSystemLanguageChanged()\0onManualExit()\0"
    "fd\0onTermSignal(int)\0request\0"
    "onInvoked(bb::system::InvokeRequest)\0"
    "onAppFullscreen()\0onAppNotFullscreen()\0"
    "onUpdateCheckFetchDone()\0"
    "onChangelogFetchDone()\0errors\0"
    "onManifestSslErrors(QList<QSslError>)\0"
    "onQueryTargetsFinished()\0"
};

void ApplicationUI::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        ApplicationUI *_t = static_cast<ApplicationUI *>(_o);
        switch (_id) {
        case 0: _t->openThreadRequested((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 1: _t->showRecalledMessagesChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->darkThemeChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 3: _t->updateCheckResult((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3])),(*reinterpret_cast< const QString(*)>(_a[4]))); break;
        case 4: _t->changelogReady((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 5: _t->shareTargetsReady((*reinterpret_cast< const QVariantList(*)>(_a[1]))); break;
        case 6: _t->eventCreated((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 7: _t->onServicePingFinished(); break;
        case 8: _t->invokeEmail((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2]))); break;
        case 9: _t->minimizeApp(); break;
        case 10: _t->setDarkTheme((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 11: { bool _r = _t->getDarkTheme();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = _r; }  break;
        case 12: _t->setShowRecalledMessages((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 13: { bool _r = _t->getShowRecalledMessages();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = _r; }  break;
        case 14: { QString _r = _t->appVersion();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = _r; }  break;
        case 15: { QString _r = _t->exportLog();
            if (_a[0]) *reinterpret_cast< QString*>(_a[0]) = _r; }  break;
        case 16: _t->checkForUpdate(); break;
        case 17: _t->fetchChangelog(); break;
        case 18: _t->copyToClipboard((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 19: { bool _r = _t->copyImageToClipboard((*reinterpret_cast< const QString(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = _r; }  break;
        case 20: _t->queryShareTargets((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 21: _t->invokeShareTarget((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 22: _t->queryShareTargetsForImage((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 23: _t->invokeShareTargetForImage((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 24: _t->openLocalFile((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 25: _t->createTodayEvent((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 26: _t->onSystemLanguageChanged(); break;
        case 27: _t->onManualExit(); break;
        case 28: _t->onTermSignal((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 29: _t->onInvoked((*reinterpret_cast< const bb::system::InvokeRequest(*)>(_a[1]))); break;
        case 30: _t->onAppFullscreen(); break;
        case 31: _t->onAppNotFullscreen(); break;
        case 32: _t->onUpdateCheckFetchDone(); break;
        case 33: _t->onChangelogFetchDone(); break;
        case 34: _t->onManifestSslErrors((*reinterpret_cast< const QList<QSslError>(*)>(_a[1]))); break;
        case 35: _t->onQueryTargetsFinished(); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData ApplicationUI::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject ApplicationUI::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_ApplicationUI,
      qt_meta_data_ApplicationUI, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &ApplicationUI::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *ApplicationUI::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *ApplicationUI::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_ApplicationUI))
        return static_cast<void*>(const_cast< ApplicationUI*>(this));
    return QObject::qt_metacast(_clname);
}

int ApplicationUI::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 36)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 36;
    }
    return _id;
}

// SIGNAL 0
void ApplicationUI::openThreadRequested(const QString & _t1, bool _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ApplicationUI::showRecalledMessagesChanged(bool _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ApplicationUI::darkThemeChanged(bool _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ApplicationUI::updateCheckResult(bool _t1, const QString & _t2, const QString & _t3, const QString & _t4)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)), const_cast<void*>(reinterpret_cast<const void*>(&_t4)) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void ApplicationUI::changelogReady(const QString & _t1, const QString & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void ApplicationUI::shareTargetsReady(const QVariantList & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void ApplicationUI::eventCreated(bool _t1, const QString & _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_END_MOC_NAMESPACE
