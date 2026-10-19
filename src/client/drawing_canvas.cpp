#include "client/drawing_canvas.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QJsonObject>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace gtd {
namespace {

QJsonObject pointJson(const QPointF& point)
{
	return {{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
}

QPointF pointFromJson(const QJsonValue& value)
{
	const QJsonObject point = value.toObject();
	return {point.value(QStringLiteral("x")).toDouble(), point.value(QStringLiteral("y")).toDouble()};
}

QString newId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QList<QList<QPointF>> clipPathOutsideCircle(const QList<QPointF>& path, const QPointF& center,
											qreal radius, bool* changed)
{
	QList<QList<QPointF>> fragments;
	QList<QPointF> current;
	*changed = false;
	if (path.isEmpty())
		return fragments;
	if (path.size() == 1) {
		const QPointF delta = path.first() - center;
		if (QPointF::dotProduct(delta, delta) <= radius * radius)
			*changed = true;
		else
			fragments.append(path);
		return fragments;
	}

	auto appendPoint = [&current](const QPointF& point) {
		if (current.isEmpty() || QLineF(current.last(), point).length() > 0.001)
			current.append(point);
	};
	auto finishFragment = [&fragments, &current] {
		if (!current.isEmpty())
			fragments.append(current);
		current.clear();
	};

	const qreal radiusSquared = radius * radius;
	for (qsizetype index = 0; index + 1 < path.size(); ++index) {
		const QPointF start = path.at(index);
		const QPointF end = path.at(index + 1);
		const QPointF direction = end - start;
		const QPointF offset = start - center;
		const qreal a = QPointF::dotProduct(direction, direction);
		const qreal b = 2.0 * QPointF::dotProduct(offset, direction);
		const qreal c = QPointF::dotProduct(offset, offset) - radiusSquared;
		bool intersects = false;
		qreal enter = 0.0;
		qreal exit = 1.0;
		if (a < 0.000001) {
			intersects = c <= 0.0;
		} else {
			const qreal discriminant = b * b - 4.0 * a * c;
			if (discriminant >= 0.0) {
				const qreal root = std::sqrt(discriminant);
				const qreal first = (-b - root) / (2.0 * a);
				const qreal second = (-b + root) / (2.0 * a);
				intersects = second >= 0.0 && first <= 1.0;
				if (intersects) {
					enter = qBound<qreal>(0.0, first, 1.0);
					exit = qBound<qreal>(0.0, second, 1.0);
				}
			} else {
				intersects = c <= 0.0;
			}
		}

		if (!intersects) {
			appendPoint(start);
			appendPoint(end);
			continue;
		}
		*changed = true;
		if (enter > 0.0) {
			appendPoint(start);
			appendPoint(start + direction * enter);
			finishFragment();
		} else {
			finishFragment();
		}
		if (exit < 1.0) {
			appendPoint(start + direction * exit);
			appendPoint(end);
		} else {
			finishFragment();
		}
	}
	finishFragment();
	return fragments;
}

}

DrawingCanvas::DrawingCanvas(QWidget* parent)
	: QWidget(parent)
{
	setMinimumSize(480, 320);
	setMouseTracking(true);
	setAutoFillBackground(false);
	layers_.append(CanvasLayer{QStringLiteral("layer-default"), QStringLiteral("Background"), 0, true, false});
	activeLayerId_ = layers_.first().id;
}

void DrawingCanvas::eraseFreehandAt(const QPointF& center, qreal radiusPixels)
{
	struct ChangedStroke {
		CanvasObject original;
		QList<QList<QPointF>> fragments;
	};
	QList<ChangedStroke> changedStrokes;
	const QPointF centerPixels(center.x() * width(), center.y() * height());
	for (const CanvasObject& object : objects_) {
		if (object.type != QStringLiteral("freehand") || object.layerId != activeLayerId_)
			continue;
		const CanvasLayer* layer = findLayer(object.layerId);
		if (!layer || !layer->visible || layer->locked)
			continue;
		QList<QPointF> path;
		path.reserve(object.points.size());
		for (const QJsonValue& value : object.points) {
			const QPointF point = pointFromJson(value);
			path.append(QPointF(point.x() * width(), point.y() * height()));
		}
		bool changed = false;
		QList<QList<QPointF>> fragments = clipPathOutsideCircle(path, centerPixels, radiusPixels, &changed);
		if (changed)
			changedStrokes.append({object, fragments});
	}

	for (const ChangedStroke& changed : changedStrokes) {
		if (changed.fragments.isEmpty()) {
			emitOperation({{QStringLiteral("action"), QStringLiteral("object_delete")},
						   {QStringLiteral("objectId"), changed.original.id}});
			continue;
		}
		for (qsizetype index = 0; index < changed.fragments.size(); ++index) {
			CanvasObject fragment = changed.original;
			if (index > 0)
				fragment.id = newId();
			fragment.points = QJsonArray{};
			for (const QPointF& point : changed.fragments.at(index))
				fragment.points.append(pointJson({point.x() / qMax(1, width()), point.y() / qMax(1, height())}));
			updateBoundsFromPoints(&fragment);
			emitObjectUpsert(fragment);
		}
	}
}

void DrawingCanvas::eraseSweep(const QPointF& from, const QPointF& to)
{
	const qreal radius = qMax<qreal>(6.0, strokeWidth_ * 2.0);
	const QPointF start(from.x() * width(), from.y() * height());
	const QPointF end(to.x() * width(), to.y() * height());
	const qreal distance = QLineF(start, end).length();
	if (distance < 0.001) {
		eraseFreehandAt(from, radius);
		return;
	}
	const int steps = qMax(1, static_cast<int>(std::ceil(distance / qMax<qreal>(1.0, radius * 0.5))));
	for (int index = 1; index <= steps; ++index) {
		const qreal fraction = static_cast<qreal>(index) / steps;
		eraseFreehandAt({from.x() + (to.x() - from.x()) * fraction,
						 from.y() + (to.y() - from.y()) * fraction}, radius);
	}
}

void DrawingCanvas::eraseObjectsAt(const QPointF& position)
{
	for (int index = objects_.size() - 1; index >= 0; --index) {
		const CanvasObject& item = objects_.at(index);
		const CanvasLayer* layer = findLayer(item.layerId);
		if (item.layerId == activeLayerId_ && layer && layer->visible && !layer->locked
			&& objectRect(item).adjusted(-8, -8, 8, 8).contains(position)) {
			emitOperation({{QStringLiteral("action"), QStringLiteral("object_delete")},
						   {QStringLiteral("objectId"), item.id}});
			return;
		}
	}
}

void DrawingCanvas::setDrawingAllowed(bool allowed)
{
	drawingAllowed_ = allowed;
	setCursor(allowed ? (tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor) : Qt::ForbiddenCursor);
	if (!allowed)
		dragMode_ = DragMode::None;
}

void DrawingCanvas::setTool(Tool tool)
{
	tool_ = tool;
	setCursor(!drawingAllowed_ ? Qt::ForbiddenCursor
							  : (tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor));
	dragMode_ = DragMode::None;
	update();
}

void DrawingCanvas::setCurrentColor(const QString& color)
{
	if (QColor(color).isValid())
		currentColor_ = color;
}

void DrawingCanvas::setStrokeWidth(int width)
{
	strokeWidth_ = qBound(1, width, 32);
}

void DrawingCanvas::setFontSize(int size)
{
	fontSize_ = qBound(8, size, 96);
}

void DrawingCanvas::setFilled(bool filled)
{
	filled_ = filled;
}

void DrawingCanvas::setScene(const QJsonArray& layers, const QJsonArray& objects)
{
	const QString oldSelection = activeLayerId_;
	layers_.clear();
	objects_.clear();
	for (const QJsonValue& value : layers) {
		if (!value.isObject())
			continue;
		const CanvasLayer layer = CanvasLayer::fromJson(value.toObject());
		if (isValidCanvasLayer(layer))
			layers_.append(layer);
	}
	if (layers_.isEmpty())
		layers_.append(CanvasLayer{QStringLiteral("layer-default"), QStringLiteral("Background"), 0, true, false});
	std::stable_sort(layers_.begin(), layers_.end(), [](const CanvasLayer& left, const CanvasLayer& right) {
		return left.order < right.order;
	});
	for (const QJsonValue& value : objects) {
		if (!value.isObject())
			continue;
		const CanvasObject item = CanvasObject::fromJson(value.toObject());
		if (isValidCanvasObject(item) && findLayer(item.layerId))
			objects_.append(item);
	}
	if (!findLayer(oldSelection))
		activeLayerId_ = layers_.first().id;
	selectedObjectId_.clear();
	emit layersChanged([this] {
		QJsonArray result;
		for (const CanvasLayer& layer : layers_)
			result.append(layer.toJson());
		return result;
	}());
	emit selectedLayerChanged(activeLayerId_);
	update();
}

void DrawingCanvas::applyOperation(const QJsonObject& operation)
{
	const QString action = operation.value(QStringLiteral("action")).toString();
	if (action == QStringLiteral("clear")) {
		objects_.clear();
		selectedObjectId_.clear();
		dragMode_ = DragMode::None;
		previewObject_ = CanvasObject{};
	} else if (action == QStringLiteral("clear_layer")) {
		const QString layerId = operation.value(QStringLiteral("layerId")).toString();
		objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&layerId](const CanvasObject& item) {
			return item.layerId == layerId;
		}), objects_.end());
		const CanvasObject* selected = findObject(selectedObjectId_);
		if (!selected || selected->layerId == layerId) {
			selectedObjectId_.clear();
			dragMode_ = DragMode::None;
			previewObject_ = CanvasObject{};
		}
	} else if (action == QStringLiteral("object_upsert")) {
		const CanvasObject item = CanvasObject::fromJson(operation.value(QStringLiteral("object")).toObject());
		if (!isValidCanvasObject(item))
			return;
		CanvasObject* existing = findObject(item.id);
		if (existing)
			*existing = item;
		else
			objects_.append(item);
	} else if (action == QStringLiteral("object_delete")) {
		const QString id = operation.value(QStringLiteral("objectId")).toString();
		objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&id](const CanvasObject& item) {
			return item.id == id;
		}), objects_.end());
		if (selectedObjectId_ == id)
			selectedObjectId_.clear();
	} else if (action == QStringLiteral("layer_upsert")) {
		const CanvasLayer layer = CanvasLayer::fromJson(operation.value(QStringLiteral("layer")).toObject());
		if (!isValidCanvasLayer(layer))
			return;
		updateLayer(layer);
		CanvasObject* selected = findObject(selectedObjectId_);
		const CanvasLayer* selectedLayer = selected ? findLayer(selected->layerId) : nullptr;
		if (selected && (selected->layerId != activeLayerId_ || !selectedLayer
						|| !selectedLayer->visible || selectedLayer->locked)) {
			selectedObjectId_.clear();
			dragMode_ = DragMode::None;
		}
	} else if (action == QStringLiteral("layer_reorder")) {
		const QJsonArray layerIds = operation.value(QStringLiteral("layerIds")).toArray();
		if (layerIds.size() != layers_.size())
			return;
		QSet<QString> seen;
		QList<CanvasLayer> reorderedLayers;
		reorderedLayers.reserve(layerIds.size());
		for (const QJsonValue& value : layerIds) {
			if (!value.isString())
				return;
			const QString id = value.toString();
			const auto existing = std::find_if(layers_.cbegin(), layers_.cend(), [&id](const CanvasLayer& layer) {
				return layer.id == id;
			});
			if (existing == layers_.cend() || seen.contains(id))
				return;
			seen.insert(id);
			reorderedLayers.append(*existing);
		}
		for (int order = 0; order < reorderedLayers.size(); ++order)
			reorderedLayers[order].order = order;
		layers_ = reorderedLayers;
		QJsonArray result;
		for (const CanvasLayer& layer : layers_)
			result.append(layer.toJson());
		emit layersChanged(result);
	} else if (action == QStringLiteral("layer_delete")) {
		const QString id = operation.value(QStringLiteral("layerId")).toString();
		layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [&id](const CanvasLayer& layer) {
			return layer.id == id;
		}), layers_.end());
		objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&id](const CanvasObject& item) {
			return item.layerId == id;
		}), objects_.end());
		for (int index = 0; index < layers_.size(); ++index)
			layers_[index].order = index;
		if (activeLayerId_ == id && !layers_.isEmpty())
			activeLayerId_ = layers_.first().id;
		emit selectedLayerChanged(activeLayerId_);
		emit layersChanged([this] {
			QJsonArray result;
			for (const CanvasLayer& layer : layers_)
				result.append(layer.toJson());
			return result;
		}());
	}
	update();
}

void DrawingCanvas::addTextAt(const QPointF& position, const QString& text)
{
	const CanvasLayer* layer = findLayer(activeLayerId_);
	if (!drawingAllowed_ || !layer || !layer->visible || layer->locked || text.trimmed().isEmpty())
		return;
	CanvasObject item;
	item.id = newId();
	item.layerId = activeLayerId_;
	item.type = QStringLiteral("text");
	item.color = currentColor_;
	item.text = text.left(256);
	item.fontSize = fontSize_;
	QFont textFont = font();
	textFont.setPixelSize(fontSize_);
	const QFontMetrics metrics(textFont);
	int textWidth = 0;
	for (const QString& line : item.text.split(QLatin1Char('\n')))
		textWidth = qMax(textWidth, metrics.horizontalAdvance(line));
	item.width = qBound(0.05, static_cast<double>(textWidth + 8) / qMax(1, width()), 0.95);
	const int availableTextWidth = qMax(1, qRound(item.width * width()) - 8);
	const QRect textBounds = metrics.boundingRect(QRect(0, 0, availableTextWidth, qMax(1, height())),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, item.text);
	item.height = qBound(0.025, static_cast<double>(textBounds.height() + 8) / qMax(1, height()), 0.9);
	item.x = qBound(0.0, position.x(), 1.0 - item.width);
	item.y = qBound(0.0, position.y(), 1.0 - item.height);
	emitObjectUpsert(item);
}

void DrawingCanvas::clearSelectedLayer()
{
	const CanvasLayer* layer = findLayer(activeLayerId_);
	if (!drawingAllowed_ || !layer || layer->locked)
		return;
	emitOperation({{QStringLiteral("action"), QStringLiteral("clear_layer")},
				   {QStringLiteral("layerId"), activeLayerId_}});
}

void DrawingCanvas::clearCanvas()
{
	if (drawingAllowed_)
		emitOperation({{QStringLiteral("action"), QStringLiteral("clear")}});
}

void DrawingCanvas::addLayer(const QString& name)
{
	if (!drawingAllowed_ || layers_.size() >= MaximumCanvasLayers || name.trimmed().isEmpty())
		return;
	CanvasLayer layer{newId(), name.trimmed().left(32), static_cast<int>(layers_.size()), true, false};
	emitLayerUpsert(layer);
	activeLayerId_ = layer.id;
	emit selectedLayerChanged(activeLayerId_);
}

void DrawingCanvas::deleteSelectedLayer()
{
	CanvasLayer* layer = findLayer(activeLayerId_);
	if (!drawingAllowed_ || !layer || layers_.size() <= 1 || layer->locked)
		return;
	emitOperation({{QStringLiteral("action"), QStringLiteral("layer_delete")},
				   {QStringLiteral("layerId"), layer->id}});
}

void DrawingCanvas::renameSelectedLayer(const QString& name)
{
	CanvasLayer* layer = findLayer(activeLayerId_);
	if (!drawingAllowed_ || !layer || name.trimmed().isEmpty())
		return;
	CanvasLayer changed = *layer;
	changed.name = name.trimmed().left(32);
	emitLayerUpsert(changed);
}

void DrawingCanvas::setSelectedLayerVisible(bool visible)
{
	CanvasLayer* layer = findLayer(activeLayerId_);
	if (!drawingAllowed_ || !layer || layer->visible == visible)
		return;
	CanvasLayer changed = *layer;
	changed.visible = visible;
	emitLayerUpsert(changed);
	if (!visible) {
		selectedObjectId_.clear();
		dragMode_ = DragMode::None;
	}
}

void DrawingCanvas::setSelectedLayerLocked(bool locked)
{
	CanvasLayer* layer = findLayer(activeLayerId_);
	if (!drawingAllowed_ || !layer || layer->locked == locked)
		return;
	CanvasLayer changed = *layer;
	changed.locked = locked;
	emitLayerUpsert(changed);
	if (locked) {
		selectedObjectId_.clear();
		dragMode_ = DragMode::None;
	}
}

void DrawingCanvas::moveSelectedLayer(int offset)
{
	if (!drawingAllowed_ || offset == 0)
		return;
	std::stable_sort(layers_.begin(), layers_.end(), [](const CanvasLayer& left, const CanvasLayer& right) {
		return left.order < right.order;
	});
	const int index = static_cast<int>(std::distance(layers_.begin(), std::find_if(
		layers_.begin(), layers_.end(), [this](const CanvasLayer& layer) { return layer.id == activeLayerId_; })));
	const int target = qBound(0, index + offset, static_cast<int>(layers_.size()) - 1);
	if (index < 0 || target == index)
		return;
	QList<CanvasLayer> reorderedLayers = layers_;
	reorderedLayers.swapItemsAt(index, target);
	QJsonArray layerIds;
	for (const CanvasLayer& layer : reorderedLayers)
		layerIds.append(layer.id);
	emitOperation({{QStringLiteral("action"), QStringLiteral("layer_reorder")},
				   {QStringLiteral("layerIds"), QJsonValue(layerIds)}});
}

QList<CanvasLayer> DrawingCanvas::layers() const
{
	return layers_;
}

QString DrawingCanvas::selectedLayerId() const
{
	return activeLayerId_;
}

void DrawingCanvas::setSelectedLayer(const QString& layerId)
{
	if (!findLayer(layerId) || activeLayerId_ == layerId)
		return;
	activeLayerId_ = layerId;
	selectedObjectId_.clear();
	dragMode_ = DragMode::None;
	previewObject_ = CanvasObject{};
	emit selectedLayerChanged(activeLayerId_);
	update();
}

QPointF DrawingCanvas::normalizedPoint(const QPointF& position) const
{
	return {qBound<qreal>(0.0, position.x() / qMax(1, width() - 1), 1.0),
			qBound<qreal>(0.0, position.y() / qMax(1, height() - 1), 1.0)};
}

QRectF DrawingCanvas::objectRect(const CanvasObject& object) const
{
	return QRectF(object.x * width(), object.y * height(), object.width * width(), object.height * height());
}

int DrawingCanvas::hitTest(const QPointF& point) const
{
	for (int index = objects_.size() - 1; index >= 0; --index) {
		const CanvasObject& item = objects_.at(index);
		const CanvasLayer* layer = findLayer(item.layerId);
		if (item.layerId == activeLayerId_ && layer && layer->visible && !layer->locked
			&& objectRect(item).adjusted(-8, -8, 8, 8).contains(point))
			return index;
	}
	return -1;
}

int DrawingCanvas::handleAt(const QRectF& bounds, const QPointF& point) const
{
	const QPointF handles[] = {bounds.topLeft(), QPointF(bounds.center().x(), bounds.top()), bounds.topRight(),
		QPointF(bounds.right(), bounds.center().y()), bounds.bottomRight(),
		QPointF(bounds.center().x(), bounds.bottom()), bounds.bottomLeft(),
		QPointF(bounds.left(), bounds.center().y())};
	for (int index = 0; index < 8; ++index) {
		if (QRectF(handles[index] - QPointF(7, 7), QSizeF(14, 14)).contains(point))
			return index;
	}
	return -1;
}

void DrawingCanvas::emitOperation(const QJsonObject& operation)
{
	applyOperation(operation);
	emit operationGenerated(operation);
}

void DrawingCanvas::emitObjectUpsert(const CanvasObject& object)
{
	emitOperation(upsertObjectOperation(object));
}

void DrawingCanvas::emitLayerUpsert(const CanvasLayer& layer)
{
	emitOperation({{QStringLiteral("action"), QStringLiteral("layer_upsert")},
				   {QStringLiteral("layer"), layer.toJson()}});
}

void DrawingCanvas::updateLayer(const CanvasLayer& layer)
{
	auto existing = std::find_if(layers_.begin(), layers_.end(), [&layer](const CanvasLayer& value) {
		return value.id == layer.id;
	});
	if (existing == layers_.end())
		layers_.append(layer);
	else
		*existing = layer;
	std::stable_sort(layers_.begin(), layers_.end(), [](const CanvasLayer& left, const CanvasLayer& right) {
		return left.order < right.order;
	});
	if (activeLayerId_.isEmpty())
		activeLayerId_ = layer.id;
	QJsonArray result;
	for (const CanvasLayer& value : layers_)
		result.append(value.toJson());
	emit layersChanged(result);
}

CanvasLayer* DrawingCanvas::findLayer(const QString& layerId)
{
	auto iterator = std::find_if(layers_.begin(), layers_.end(), [&layerId](const CanvasLayer& layer) {
		return layer.id == layerId;
	});
	return iterator == layers_.end() ? nullptr : &*iterator;
}

const CanvasLayer* DrawingCanvas::findLayer(const QString& layerId) const
{
	auto iterator = std::find_if(layers_.cbegin(), layers_.cend(), [&layerId](const CanvasLayer& layer) {
		return layer.id == layerId;
	});
	return iterator == layers_.cend() ? nullptr : &*iterator;
}

CanvasObject* DrawingCanvas::findObject(const QString& objectId)
{
	auto iterator = std::find_if(objects_.begin(), objects_.end(), [&objectId](const CanvasObject& item) {
		return item.id == objectId;
	});
	return iterator == objects_.end() ? nullptr : &*iterator;
}

void DrawingCanvas::updateBoundsFromPoints(CanvasObject* object) const
{
	if (!object || object->points.isEmpty())
		return;
	double left = 1.0;
	double top = 1.0;
	double right = 0.0;
	double bottom = 0.0;
	for (const QJsonValue& value : object->points) {
		const QPointF point = pointFromJson(value);
		left = qMin(left, point.x());
		top = qMin(top, point.y());
		right = qMax(right, point.x());
		bottom = qMax(bottom, point.y());
	}
	object->x = left;
	object->y = top;
	object->width = right - left;
	object->height = bottom - top;
}

void DrawingCanvas::paintObject(QPainter& painter, const CanvasObject& object) const
{
	QColor color(object.color);
	if (!color.isValid())
		color = QColor(QStringLiteral("#202124"));
	QPen pen(color, qMax<qreal>(1.0, object.strokeWidth * qMin(width(), height()) / 600.0),
			 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	painter.setPen(pen);
	painter.setBrush(object.filled ? QBrush(color) : Qt::NoBrush);
	const QRectF bounds = objectRect(object);
	if (object.type == QStringLiteral("rectangle")) {
		painter.drawRect(bounds);
	} else if (object.type == QStringLiteral("ellipse")) {
		painter.drawEllipse(bounds);
	} else if (object.type == QStringLiteral("line")) {
		if (object.points.size() >= 2)
			painter.drawLine(pointFromJson(object.points.at(0)).x() * width(),
							 pointFromJson(object.points.at(0)).y() * height(),
							 pointFromJson(object.points.at(1)).x() * width(),
							 pointFromJson(object.points.at(1)).y() * height());
	} else if (object.type == QStringLiteral("freehand")) {
		if (object.points.isEmpty())
			return;
		QPainterPath path;
		const QPointF first = pointFromJson(object.points.first());
		path.moveTo(first.x() * width(), first.y() * height());
		QPointF last = first;
		for (qsizetype index = 1; index < object.points.size(); ++index) {
			last = pointFromJson(object.points.at(index));
			path.lineTo(last.x() * width(), last.y() * height());
		}
		painter.drawPath(path);
		if (object.points.size() == 1)
			painter.drawPoint(last.x() * width(), last.y() * height());
	} else if (object.type == QStringLiteral("text")) {
		QFont textFont = font();
		textFont.setPixelSize(qMax(8, object.fontSize * width() / 1024));
		painter.setFont(textFont);
		painter.setBrush(Qt::NoBrush);
		painter.drawText(bounds, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, object.text);
	}
}

void DrawingCanvas::updateDrag(const QPointF& position)
{
	CanvasObject* item = findObject(selectedObjectId_);
	if (!item)
		return;
	const CanvasLayer* layer = findLayer(item->layerId);
	if (item->layerId != activeLayerId_ || !layer || !layer->visible || layer->locked) {
		selectedObjectId_.clear();
		dragMode_ = DragMode::None;
		return;
	}
	const QPointF current = normalizedPoint(position);
	CanvasObject changed = originalObject_;
	if (dragMode_ == DragMode::Move) {
		double dx = current.x() - dragStart_.x();
		double dy = current.y() - dragStart_.y();
		dx = qBound(-changed.x, dx, 1.0 - changed.x - changed.width);
		dy = qBound(-changed.y, dy, 1.0 - changed.y - changed.height);
		changed.x += dx;
		changed.y += dy;
		for (qsizetype index = 0; index < changed.points.size(); ++index) {
			QPointF point = pointFromJson(changed.points.at(index));
			changed.points[index] = pointJson({point.x() + dx, point.y() + dy});
		}
	} else if (dragMode_ == DragMode::Resize) {
		double left = changed.x;
		double top = changed.y;
		double right = changed.x + changed.width;
		double bottom = changed.y + changed.height;
		constexpr double minimum = 0.004;
		if (resizeHandle_ == 0 || resizeHandle_ == 6 || resizeHandle_ == 7)
			left = qBound(0.0, current.x(), right - minimum);
		if (resizeHandle_ == 2 || resizeHandle_ == 3 || resizeHandle_ == 4)
			right = qBound(left + minimum, current.x(), 1.0);
		if (resizeHandle_ == 0 || resizeHandle_ == 1 || resizeHandle_ == 2)
			top = qBound(0.0, current.y(), bottom - minimum);
		if (resizeHandle_ == 4 || resizeHandle_ == 5 || resizeHandle_ == 6)
			bottom = qBound(top + minimum, current.y(), 1.0);
		for (qsizetype index = 0; index < changed.points.size(); ++index) {
			const QPointF oldPoint = pointFromJson(changed.points.at(index));
			const double relativeX = changed.width > 0.000001 ? (oldPoint.x() - changed.x) / changed.width : 0.5;
			const double relativeY = changed.height > 0.000001 ? (oldPoint.y() - changed.y) / changed.height : 0.5;
			changed.points[index] = pointJson({left + relativeX * (right - left),
												top + relativeY * (bottom - top)});
		}
		changed.x = left;
		changed.y = top;
		changed.width = right - left;
		changed.height = bottom - top;
	}
	emitObjectUpsert(changed);
}

QJsonObject DrawingCanvas::upsertObjectOperation(const CanvasObject& object)
{
	return {{QStringLiteral("action"), QStringLiteral("object_upsert")},
			{QStringLiteral("object"), object.toJson()}};
}

void DrawingCanvas::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.fillRect(rect(), Qt::white);
	painter.setPen(QPen(QColor(QStringLiteral("#c9ced6")), 1));
	painter.drawRect(rect().adjusted(0, 0, -1, -1));
	QList<CanvasLayer> paintOrder = layers_;
	std::stable_sort(paintOrder.begin(), paintOrder.end(), [](const CanvasLayer& left, const CanvasLayer& right) {
		return left.order < right.order;
	});
	for (const CanvasLayer& layer : paintOrder) {
		if (!layer.visible)
			continue;
		for (const CanvasObject& object : objects_) {
			if (object.layerId == layer.id)
				paintObject(painter, object);
		}
	}
	if (!previewObject_.id.isEmpty())
		paintObject(painter, previewObject_);

	CanvasObject* selected = findObject(selectedObjectId_);
	if (selected && drawingAllowed_) {
		QRectF bounds = objectRect(*selected);
		if (bounds.width() < 1.0)
			bounds.setWidth(1.0);
		if (bounds.height() < 1.0)
			bounds.setHeight(1.0);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(QColor(QStringLiteral("#1677ff")), 1, Qt::DashLine));
		painter.drawRect(bounds);
		painter.setPen(QPen(QColor(QStringLiteral("#1677ff")), 1));
		painter.setBrush(Qt::white);
		const QPointF handles[] = {bounds.topLeft(), QPointF(bounds.center().x(), bounds.top()),
			bounds.topRight(), QPointF(bounds.right(), bounds.center().y()), bounds.bottomRight(),
			QPointF(bounds.center().x(), bounds.bottom()), bounds.bottomLeft(),
			QPointF(bounds.left(), bounds.center().y())};
		for (const QPointF& handle : handles)
			painter.drawRect(QRectF(handle - QPointF(4, 4), QSizeF(8, 8)));
	}
}

void DrawingCanvas::mousePressEvent(QMouseEvent* event)
{
	if (!drawingAllowed_ || event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const QPointF point = normalizedPoint(event->position());
	const CanvasLayer* layer = findLayer(activeLayerId_);
	if (!layer || layer->locked || !layer->visible) {
		event->accept();
		return;
	}

	if (tool_ == Tool::Select) {
		const int hit = hitTest(event->position());
		if (hit < 0) {
			selectedObjectId_.clear();
			dragMode_ = DragMode::None;
		} else {
			const CanvasObject& item = objects_.at(hit);
			selectedObjectId_ = item.id;
			originalObject_ = item;
			dragStart_ = point;
			resizeHandle_ = handleAt(objectRect(item), event->position());
			dragMode_ = resizeHandle_ >= 0 ? DragMode::Resize : DragMode::Move;
		}
		update();
		event->accept();
		return;
	}
	if (tool_ == Tool::Eraser) {
		dragMode_ = DragMode::Erase;
		dragStart_ = point;
		eraseSweep(point, point);
		event->accept();
		return;
	}
	if (tool_ == Tool::ObjectEraser) {
		dragMode_ = DragMode::Erase;
		dragStart_ = point;
		eraseObjectsAt(event->position());
		event->accept();
		return;
	}
	if (tool_ == Tool::Text) {
		emit textRequested(point);
		event->accept();
		return;
	}

	previewObject_ = CanvasObject{};
	previewObject_.id = newId();
	previewObject_.layerId = activeLayerId_;
	previewObject_.color = currentColor_;
	previewObject_.strokeWidth = strokeWidth_;
	previewObject_.filled = filled_;
	previewObject_.x = point.x();
	previewObject_.y = point.y();
	previewObject_.type = tool_ == Tool::Pen ? QStringLiteral("freehand")
		: tool_ == Tool::Rectangle ? QStringLiteral("rectangle")
		: tool_ == Tool::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("line");
	if (tool_ == Tool::Pen) {
		previewObject_.points.append(pointJson(point));
		updateBoundsFromPoints(&previewObject_);
		selectedObjectId_ = previewObject_.id;
		emitObjectUpsert(previewObject_);
		previewObject_.id.clear();
	} else if (tool_ == Tool::Line) {
		previewObject_.points.append(pointJson(point));
		previewObject_.points.append(pointJson(point));
	}
	dragStart_ = point;
	dragMode_ = DragMode::Draw;
	update();
	event->accept();
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* event)
{
	if (!drawingAllowed_) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	if (dragMode_ == DragMode::Move || dragMode_ == DragMode::Resize) {
		updateDrag(event->position());
	} else if (dragMode_ == DragMode::Erase && (event->buttons() & Qt::LeftButton)) {
		const QPointF point = normalizedPoint(event->position());
		if (tool_ == Tool::Eraser)
			eraseSweep(dragStart_, point);
		else if (tool_ == Tool::ObjectEraser)
			eraseObjectsAt(event->position());
		dragStart_ = point;
	} else if (dragMode_ == DragMode::Draw && (event->buttons() & Qt::LeftButton)) {
		const QPointF point = normalizedPoint(event->position());
		if (tool_ == Tool::Pen) {
			CanvasObject* item = findObject(selectedObjectId_);
			if (item && item->points.size() < MaximumObjectPoints) {
				CanvasObject updated = *item;
				updated.points.append(pointJson(point));
				updateBoundsFromPoints(&updated);
				emitObjectUpsert(updated);
			}
		} else if (tool_ == Tool::Line) {
			previewObject_.points[1] = pointJson(point);
			updateBoundsFromPoints(&previewObject_);
			update();
		} else {
			const qreal left = qMin(dragStart_.x(), point.x());
			const qreal top = qMin(dragStart_.y(), point.y());
			previewObject_.x = left;
			previewObject_.y = top;
			previewObject_.width = qAbs(point.x() - dragStart_.x());
			previewObject_.height = qAbs(point.y() - dragStart_.y());
			update();
		}
	} else if (tool_ == Tool::Select) {
		const int hit = hitTest(event->position());
		if (hit >= 0 && selectedObjectId_ == objects_.at(hit).id)
			setCursor(handleAt(objectRect(objects_.at(hit)), event->position()) >= 0
						  ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
		else
			setCursor(hit >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
	}
	event->accept();
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		if (dragMode_ == DragMode::Draw && tool_ != Tool::Pen) {
			if (tool_ == Tool::Line)
				updateBoundsFromPoints(&previewObject_);
			if (previewObject_.width > 0.001 || previewObject_.height > 0.001)
				emitObjectUpsert(previewObject_);
			previewObject_ = CanvasObject{};
		}
		if (dragMode_ == DragMode::Draw && tool_ == Tool::Pen)
			selectedObjectId_.clear();
		dragMode_ = DragMode::None;
		resizeHandle_ = -1;
		update();
	}
	QWidget::mouseReleaseEvent(event);
}

}
