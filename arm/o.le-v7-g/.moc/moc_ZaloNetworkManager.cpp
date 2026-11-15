/****************************************************************************
** Meta object code from reading C++ file 'ZaloNetworkManager.hpp'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/utils/ZaloNetworkManager.hpp"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ZaloNetworkManager.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_ZaloFallbackReply[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      19,   18,   18,   18, 0x08,
      37,   18,   18,   18, 0x08,
      54,   18,   18,   18, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_ZaloFallbackReply[] = {
    "ZaloFallbackReply\0\0onRealReadyRead()\0"
    "onRealFinished()\0onRawDone()\0"
};

void ZaloFallbackReply::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        ZaloFallbackReply *_t = static_cast<ZaloFallbackReply *>(_o);
        switch (_id) {
        case 0: _t->onRealReadyRead(); break;
        case 1: _t->onRealFinished(); break;
        case 2: _t->onRawDone(); break;
        default: ;
        }
    }
    Q_UNUSED(_a);
}

const QMetaObjectExtraData ZaloFallbackReply::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject ZaloFallbackReply::staticMetaObject = {
    { &QNetworkReply::staticMetaObject, qt_meta_stringdata_ZaloFallbackReply,
      qt_meta_data_ZaloFallbackReply, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &ZaloFallbackReply::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *ZaloFallbackReply::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *ZaloFallbackReply::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_ZaloFallbackReply))
        return static_cast<void*>(const_cast< ZaloFallbackReply*>(this));
    return QNetworkReply::qt_metacast(_clname);
}

int ZaloFallbackReply::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QNetworkReply::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}
static const uint qt_meta_data_ZaloNetworkAccessManager[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

static const char qt_meta_stringdata_ZaloNetworkAccessManager[] = {
    "ZaloNetworkAccessManager\0"
};

void ZaloNetworkAccessManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

const QMetaObjectExtraData ZaloNetworkAccessManager::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject ZaloNetworkAccessManager::staticMetaObject = {
    { &QNetworkAccessManager::staticMetaObject, qt_meta_stringdata_ZaloNetworkAccessManager,
      qt_meta_data_ZaloNetworkAccessManager, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &ZaloNetworkAccessManager::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *ZaloNetworkAccessManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *ZaloNetworkAccessManager::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_ZaloNetworkAccessManager))
        return static_cast<void*>(const_cast< ZaloNetworkAccessManager*>(this));
    return QNetworkAccessManager::qt_metacast(_clname);
}

int ZaloNetworkAccessManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QNetworkAccessManager::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    return _id;
}
QT_END_MOC_NAMESPACE
