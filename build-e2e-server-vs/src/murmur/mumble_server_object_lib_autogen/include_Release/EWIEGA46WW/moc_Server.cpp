/****************************************************************************
** Meta object code from reading C++ file 'Server.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../src/murmur/Server.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'Server.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN9SslServerE_t {};
} // unnamed namespace

template <> constexpr inline auto SslServer::qt_create_metaobjectdata<qt_meta_tag_ZN9SslServerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SslServer"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SslServer, qt_meta_tag_ZN9SslServerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SslServer::staticMetaObject = { {
    QMetaObject::SuperData::link<QTcpServer::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9SslServerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9SslServerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9SslServerE_t>.metaTypes,
    nullptr
} };

void SslServer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SslServer *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *SslServer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SslServer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9SslServerE_t>.strings))
        return static_cast<void*>(this);
    return QTcpServer::qt_metacast(_clname);
}

int SslServer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QTcpServer::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN6ServerE_t {};
} // unnamed namespace

template <> constexpr inline auto Server::qt_create_metaobjectdata<qt_meta_tag_ZN6ServerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "Server",
        "reqSync",
        "",
        "tcpTransmit",
        "id",
        "registerUserSig",
        "int&",
        "QMap<int,QString>",
        "unregisterUserSig",
        "getRegisteredUsersSig",
        "QMap<int,QString>&",
        "getRegistrationSig",
        "authenticateSig",
        "QString&",
        "QList<QSslCertificate>",
        "setInfoSig",
        "setTextureSig",
        "idToNameSig",
        "nameToIdSig",
        "idToTextureSig",
        "QByteArray&",
        "userStateChanged",
        "const User*",
        "userTextMessage",
        "TextMessage",
        "userConnected",
        "userDisconnected",
        "channelStateChanged",
        "const Channel*",
        "channelCreated",
        "channelRemoved",
        "textMessageFilterSig",
        "MumbleProto::TextMessage&",
        "contextAction",
        "regSslError",
        "QList<QSslError>",
        "finished",
        "update",
        "newClient",
        "connectionClosed",
        "QAbstractSocket::SocketError",
        "sslError",
        "message",
        "Mumble::Protocol::TCPMessageType",
        "ServerUser*",
        "cCon",
        "checkTimeout",
        "tcpTransmitData",
        "doSync",
        "encrypted",
        "udpActivated"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'reqSync'
        QtMocHelpers::SignalData<void(unsigned int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 2 },
        }}),
        // Signal 'tcpTransmit'
        QtMocHelpers::SignalData<void(QByteArray, unsigned int)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 2 }, { QMetaType::UInt, 4 },
        }}),
        // Signal 'registerUserSig'
        QtMocHelpers::SignalData<void(int &, const QMap<int,QString> &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { 0x80000000 | 7, 2 },
        }}),
        // Signal 'unregisterUserSig'
        QtMocHelpers::SignalData<void(int &, int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { QMetaType::Int, 2 },
        }}),
        // Signal 'getRegisteredUsersSig'
        QtMocHelpers::SignalData<void(const QString &, QMap<int,QString> &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 2 }, { 0x80000000 | 10, 2 },
        }}),
        // Signal 'getRegistrationSig'
        QtMocHelpers::SignalData<void(int &, int, QMap<int,QString> &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { QMetaType::Int, 2 }, { 0x80000000 | 10, 2 },
        }}),
        // Signal 'authenticateSig'
        QtMocHelpers::SignalData<void(int &, QString &, int, const QList<QSslCertificate> &, const QString &, bool, const QString &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { 0x80000000 | 13, 2 }, { QMetaType::Int, 2 }, { 0x80000000 | 14, 2 },
            { QMetaType::QString, 2 }, { QMetaType::Bool, 2 }, { QMetaType::QString, 2 },
        }}),
        // Signal 'setInfoSig'
        QtMocHelpers::SignalData<void(int &, int, const QMap<int,QString> &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { QMetaType::Int, 2 }, { 0x80000000 | 7, 2 },
        }}),
        // Signal 'setTextureSig'
        QtMocHelpers::SignalData<void(int &, int, const QByteArray &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { QMetaType::Int, 2 }, { QMetaType::QByteArray, 2 },
        }}),
        // Signal 'idToNameSig'
        QtMocHelpers::SignalData<void(QString &, int)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 2 }, { QMetaType::Int, 2 },
        }}),
        // Signal 'nameToIdSig'
        QtMocHelpers::SignalData<void(int &, const QString &)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { QMetaType::QString, 2 },
        }}),
        // Signal 'idToTextureSig'
        QtMocHelpers::SignalData<void(QByteArray &, int)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 20, 2 }, { QMetaType::Int, 2 },
        }}),
        // Signal 'userStateChanged'
        QtMocHelpers::SignalData<void(const User *)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 22, 2 },
        }}),
        // Signal 'userTextMessage'
        QtMocHelpers::SignalData<void(const User *, const TextMessage &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 22, 2 }, { 0x80000000 | 24, 2 },
        }}),
        // Signal 'userConnected'
        QtMocHelpers::SignalData<void(const User *)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 22, 2 },
        }}),
        // Signal 'userDisconnected'
        QtMocHelpers::SignalData<void(const User *)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 22, 2 },
        }}),
        // Signal 'channelStateChanged'
        QtMocHelpers::SignalData<void(const Channel *)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 28, 2 },
        }}),
        // Signal 'channelCreated'
        QtMocHelpers::SignalData<void(const Channel *)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 28, 2 },
        }}),
        // Signal 'channelRemoved'
        QtMocHelpers::SignalData<void(const Channel *)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 28, 2 },
        }}),
        // Signal 'textMessageFilterSig'
        QtMocHelpers::SignalData<void(int &, const User *, MumbleProto::TextMessage &)>(31, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 2 }, { 0x80000000 | 22, 2 }, { 0x80000000 | 32, 2 },
        }}),
        // Signal 'contextAction'
        QtMocHelpers::SignalData<void(const User *, const QString &, unsigned int, int)>(33, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 22, 2 }, { QMetaType::QString, 2 }, { QMetaType::UInt, 2 }, { QMetaType::Int, 2 },
        }}),
        // Slot 'regSslError'
        QtMocHelpers::SlotData<void(const QList<QSslError> &)>(34, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 35, 2 },
        }}),
        // Slot 'finished'
        QtMocHelpers::SlotData<void()>(36, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'update'
        QtMocHelpers::SlotData<void()>(37, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'newClient'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'connectionClosed'
        QtMocHelpers::SlotData<void(QAbstractSocket::SocketError, const QString &)>(39, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 40, 2 }, { QMetaType::QString, 2 },
        }}),
        // Slot 'sslError'
        QtMocHelpers::SlotData<void(const QList<QSslError> &)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 35, 2 },
        }}),
        // Slot 'message'
        QtMocHelpers::SlotData<void(Mumble::Protocol::TCPMessageType, const QByteArray &, ServerUser *)>(42, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 43, 2 }, { QMetaType::QByteArray, 2 }, { 0x80000000 | 44, 45 },
        }}),
        // Slot 'message'
        QtMocHelpers::SlotData<void(Mumble::Protocol::TCPMessageType, const QByteArray &)>(42, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 43, 2 }, { QMetaType::QByteArray, 2 },
        }}),
        // Slot 'checkTimeout'
        QtMocHelpers::SlotData<void()>(46, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'tcpTransmitData'
        QtMocHelpers::SlotData<void(QByteArray, unsigned int)>(47, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 2 }, { QMetaType::UInt, 2 },
        }}),
        // Slot 'doSync'
        QtMocHelpers::SlotData<void(unsigned int)>(48, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::UInt, 2 },
        }}),
        // Slot 'encrypted'
        QtMocHelpers::SlotData<void()>(49, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'udpActivated'
        QtMocHelpers::SlotData<void(int)>(50, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 2 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<Server, qt_meta_tag_ZN6ServerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject Server::staticMetaObject = { {
    QMetaObject::SuperData::link<QThread::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6ServerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6ServerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6ServerE_t>.metaTypes,
    nullptr
} };

void Server::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Server *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->reqSync((*reinterpret_cast< std::add_pointer_t<uint>>(_a[1]))); break;
        case 1: _t->tcpTransmit((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint>>(_a[2]))); break;
        case 2: _t->registerUserSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QMap<int,QString>>>(_a[2]))); break;
        case 3: _t->unregisterUserSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->getRegisteredUsersSig((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QMap<int,QString>&>>(_a[2]))); break;
        case 5: _t->getRegistrationSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QMap<int,QString>&>>(_a[3]))); break;
        case 6: _t->authenticateSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString&>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QList<QSslCertificate>>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[6])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[7]))); break;
        case 7: _t->setInfoSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QMap<int,QString>>>(_a[3]))); break;
        case 8: _t->setTextureSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[3]))); break;
        case 9: _t->idToNameSig((*reinterpret_cast< std::add_pointer_t<QString&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 10: _t->nameToIdSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 11: _t->idToTextureSig((*reinterpret_cast< std::add_pointer_t<QByteArray&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 12: _t->userStateChanged((*reinterpret_cast< std::add_pointer_t<const User*>>(_a[1]))); break;
        case 13: _t->userTextMessage((*reinterpret_cast< std::add_pointer_t<const User*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<TextMessage>>(_a[2]))); break;
        case 14: _t->userConnected((*reinterpret_cast< std::add_pointer_t<const User*>>(_a[1]))); break;
        case 15: _t->userDisconnected((*reinterpret_cast< std::add_pointer_t<const User*>>(_a[1]))); break;
        case 16: _t->channelStateChanged((*reinterpret_cast< std::add_pointer_t<const Channel*>>(_a[1]))); break;
        case 17: _t->channelCreated((*reinterpret_cast< std::add_pointer_t<const Channel*>>(_a[1]))); break;
        case 18: _t->channelRemoved((*reinterpret_cast< std::add_pointer_t<const Channel*>>(_a[1]))); break;
        case 19: _t->textMessageFilterSig((*reinterpret_cast< std::add_pointer_t<int&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<const User*>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<MumbleProto::TextMessage&>>(_a[3]))); break;
        case 20: _t->contextAction((*reinterpret_cast< std::add_pointer_t<const User*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[4]))); break;
        case 21: _t->regSslError((*reinterpret_cast< std::add_pointer_t<QList<QSslError>>>(_a[1]))); break;
        case 22: _t->finished(); break;
        case 23: _t->update(); break;
        case 24: _t->newClient(); break;
        case 25: _t->connectionClosed((*reinterpret_cast< std::add_pointer_t<QAbstractSocket::SocketError>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 26: _t->sslError((*reinterpret_cast< std::add_pointer_t<QList<QSslError>>>(_a[1]))); break;
        case 27: _t->message((*reinterpret_cast< std::add_pointer_t<Mumble::Protocol::TCPMessageType>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<ServerUser*>>(_a[3]))); break;
        case 28: _t->message((*reinterpret_cast< std::add_pointer_t<Mumble::Protocol::TCPMessageType>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[2]))); break;
        case 29: _t->checkTimeout(); break;
        case 30: _t->tcpTransmitData((*reinterpret_cast< std::add_pointer_t<QByteArray>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<uint>>(_a[2]))); break;
        case 31: _t->doSync((*reinterpret_cast< std::add_pointer_t<uint>>(_a[1]))); break;
        case 32: _t->encrypted(); break;
        case 33: _t->udpActivated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 3:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QSslCertificate> >(); break;
            }
            break;
        case 21:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QSslError> >(); break;
            }
            break;
        case 25:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QAbstractSocket::SocketError >(); break;
            }
            break;
        case 26:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<QSslError> >(); break;
            }
            break;
        case 27:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 2:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< ServerUser* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (Server::*)(unsigned int )>(_a, &Server::reqSync, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(QByteArray , unsigned int )>(_a, &Server::tcpTransmit, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , const QMap<int,QString> & )>(_a, &Server::registerUserSig, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , int )>(_a, &Server::unregisterUserSig, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const QString & , QMap<int,QString> & )>(_a, &Server::getRegisteredUsersSig, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , int , QMap<int,QString> & )>(_a, &Server::getRegistrationSig, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , QString & , int , const QList<QSslCertificate> & , const QString & , bool , const QString & )>(_a, &Server::authenticateSig, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , int , const QMap<int,QString> & )>(_a, &Server::setInfoSig, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , int , const QByteArray & )>(_a, &Server::setTextureSig, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(QString & , int )>(_a, &Server::idToNameSig, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , const QString & )>(_a, &Server::nameToIdSig, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(QByteArray & , int )>(_a, &Server::idToTextureSig, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const User * )>(_a, &Server::userStateChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const User * , const TextMessage & )>(_a, &Server::userTextMessage, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const User * )>(_a, &Server::userConnected, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const User * )>(_a, &Server::userDisconnected, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const Channel * )>(_a, &Server::channelStateChanged, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const Channel * )>(_a, &Server::channelCreated, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const Channel * )>(_a, &Server::channelRemoved, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(int & , const User * , MumbleProto::TextMessage & )>(_a, &Server::textMessageFilterSig, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (Server::*)(const User * , const QString & , unsigned int , int )>(_a, &Server::contextAction, 20))
            return;
    }
}

const QMetaObject *Server::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Server::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6ServerE_t>.strings))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int Server::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 34)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 34;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 34)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 34;
    }
    return _id;
}

// SIGNAL 0
void Server::reqSync(unsigned int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void Server::tcpTransmit(QByteArray _t1, unsigned int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void Server::registerUserSig(int & _t1, const QMap<int,QString> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void Server::unregisterUserSig(int & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void Server::getRegisteredUsersSig(const QString & _t1, QMap<int,QString> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void Server::getRegistrationSig(int & _t1, int _t2, QMap<int,QString> & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3);
}

// SIGNAL 6
void Server::authenticateSig(int & _t1, QString & _t2, int _t3, const QList<QSslCertificate> & _t4, const QString & _t5, bool _t6, const QString & _t7)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2, _t3, _t4, _t5, _t6, _t7);
}

// SIGNAL 7
void Server::setInfoSig(int & _t1, int _t2, const QMap<int,QString> & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1, _t2, _t3);
}

// SIGNAL 8
void Server::setTextureSig(int & _t1, int _t2, const QByteArray & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2, _t3);
}

// SIGNAL 9
void Server::idToNameSig(QString & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1, _t2);
}

// SIGNAL 10
void Server::nameToIdSig(int & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1, _t2);
}

// SIGNAL 11
void Server::idToTextureSig(QByteArray & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void Server::userStateChanged(const User * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void Server::userTextMessage(const User * _t1, const TextMessage & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}

// SIGNAL 14
void Server::userConnected(const User * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}

// SIGNAL 15
void Server::userDisconnected(const User * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1);
}

// SIGNAL 16
void Server::channelStateChanged(const Channel * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1);
}

// SIGNAL 17
void Server::channelCreated(const Channel * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1);
}

// SIGNAL 18
void Server::channelRemoved(const Channel * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 18, nullptr, _t1);
}

// SIGNAL 19
void Server::textMessageFilterSig(int & _t1, const User * _t2, MumbleProto::TextMessage & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 19, nullptr, _t1, _t2, _t3);
}

// SIGNAL 20
void Server::contextAction(const User * _t1, const QString & _t2, unsigned int _t3, int _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 20, nullptr, _t1, _t2, _t3, _t4);
}
QT_WARNING_POP
