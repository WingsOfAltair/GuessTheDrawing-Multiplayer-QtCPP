/****************************************************************************
** Meta object code from reading C++ file 'drawing_canvas.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../src/client/drawing_canvas.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'drawing_canvas.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3gtd13DrawingCanvasE_t {};
} // unnamed namespace

template <> constexpr inline auto gtd::DrawingCanvas::qt_create_metaobjectdata<qt_meta_tag_ZN3gtd13DrawingCanvasE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "gtd::DrawingCanvas",
        "operationGenerated",
        "",
        "operation",
        "layersChanged",
        "layers",
        "selectedLayerChanged",
        "layerId",
        "textRequested",
        "position"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'operationGenerated'
        QtMocHelpers::SignalData<void(const QJsonObject &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QJsonObject, 3 },
        }}),
        // Signal 'layersChanged'
        QtMocHelpers::SignalData<void(const QJsonArray &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QJsonArray, 5 },
        }}),
        // Signal 'selectedLayerChanged'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Signal 'textRequested'
        QtMocHelpers::SignalData<void(const QPointF &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QPointF, 9 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DrawingCanvas, qt_meta_tag_ZN3gtd13DrawingCanvasE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject gtd::DrawingCanvas::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3gtd13DrawingCanvasE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3gtd13DrawingCanvasE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3gtd13DrawingCanvasE_t>.metaTypes,
    nullptr
} };

void gtd::DrawingCanvas::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DrawingCanvas *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->operationGenerated((*reinterpret_cast< std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 1: _t->layersChanged((*reinterpret_cast< std::add_pointer_t<QJsonArray>>(_a[1]))); break;
        case 2: _t->selectedLayerChanged((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->textRequested((*reinterpret_cast< std::add_pointer_t<QPointF>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DrawingCanvas::*)(const QJsonObject & )>(_a, &DrawingCanvas::operationGenerated, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DrawingCanvas::*)(const QJsonArray & )>(_a, &DrawingCanvas::layersChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DrawingCanvas::*)(const QString & )>(_a, &DrawingCanvas::selectedLayerChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DrawingCanvas::*)(const QPointF & )>(_a, &DrawingCanvas::textRequested, 3))
            return;
    }
}

const QMetaObject *gtd::DrawingCanvas::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *gtd::DrawingCanvas::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3gtd13DrawingCanvasE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int gtd::DrawingCanvas::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void gtd::DrawingCanvas::operationGenerated(const QJsonObject & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void gtd::DrawingCanvas::layersChanged(const QJsonArray & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void gtd::DrawingCanvas::selectedLayerChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void gtd::DrawingCanvas::textRequested(const QPointF & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
