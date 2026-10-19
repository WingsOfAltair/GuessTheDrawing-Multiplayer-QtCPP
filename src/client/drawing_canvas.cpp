#include "client/drawing_canvas.h"

#include <QColor>
#include <QBitArray>
#include <QBuffer>
#include <QFont>
#include <QFontMetrics>
#include <QJsonObject>
#include <QImage>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QQueue>
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

quint32 stableSeed(const QString& id)
{
	quint32 seed = 2166136261u;
	for (const char value : id.toUtf8())
		seed = (seed ^ static_cast<quint8>(value)) * 16777619u;
	return seed;
}

quint32 nextRandom(quint32* state)
{
	*state = *state * 1664525u + 1013904223u;
	return *state;
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
		if ((object.type != QStringLiteral("freehand") && object.type != QStringLiteral("brush")
			&& object.type != QStringLiteral("airbrush") && object.type != QStringLiteral("curve"))
			|| object.layerId != activeLayerId_)
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
	if (!allowed) {
		if (polygonInProgress_)
			cancelHistoryTransaction();
		else
			endHistoryTransaction();
		dragMode_ = DragMode::None;
		polygonInProgress_ = false;
		previewObject_ = CanvasObject{};
	}
}

void DrawingCanvas::setTool(Tool tool)
{
	if (tool_ != tool && polygonInProgress_) {
		cancelHistoryTransaction();
		polygonInProgress_ = false;
		previewObject_ = CanvasObject{};
	} else if (tool_ != tool) {
		endHistoryTransaction();
	}
	tool_ = tool;
	setCursor(!drawingAllowed_ ? Qt::ForbiddenCursor
							  : (tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor));
	dragMode_ = DragMode::None;
	update();
}

void DrawingCanvas::fillAt(const QPointF& position)
{
	const qreal scale = qMin<qreal>(1.0,
		static_cast<qreal>(MaximumCanvasImageDimension) / qMax(width(), height()));
	const QSize rasterSize(qMax(1, qRound(width() * scale)), qMax(1, qRound(height() * scale)));
	QImage composite(rasterSize, QImage::Format_ARGB32_Premultiplied);
	composite.fill(Qt::white);
	QPainter scenePainter(&composite);
	scenePainter.setRenderHint(QPainter::Antialiasing);
	scenePainter.scale(static_cast<qreal>(rasterSize.width()) / qMax(1, width()),
		static_cast<qreal>(rasterSize.height()) / qMax(1, height()));
	QList<CanvasLayer> paintOrder = layers_;
	std::stable_sort(paintOrder.begin(), paintOrder.end(), [](const CanvasLayer& left, const CanvasLayer& right) {
		return left.order < right.order;
	});
	for (const CanvasLayer& layer : paintOrder) {
		if (!layer.visible)
			continue;
		for (const CanvasObject& object : objects_) {
			if (object.layerId == layer.id)
				paintObject(scenePainter, object);
		}
	}
	scenePainter.end();

	const int seedX = qBound(0, qRound(position.x() * (rasterSize.width() - 1)), rasterSize.width() - 1);
	const int seedY = qBound(0, qRound(position.y() * (rasterSize.height() - 1)), rasterSize.height() - 1);
	const QColor target = composite.pixelColor(seedX, seedY);
	QImage fillImage(rasterSize, QImage::Format_ARGB32_Premultiplied);
	fillImage.fill(Qt::transparent);
	QBitArray visited(rasterSize.width() * rasterSize.height(), false);
	QQueue<int> pending;
	const int seedIndex = seedY * rasterSize.width() + seedX;
	visited.setBit(seedIndex, true);
	pending.enqueue(seedIndex);
	int left = seedX;
	int right = seedX;
	int top = seedY;
	int bottom = seedY;
	const auto enqueue = [&](int x, int y) {
		if (x < 0 || x >= rasterSize.width() || y < 0 || y >= rasterSize.height())
			return;
		const int index = y * rasterSize.width() + x;
		if (!visited.testBit(index)) {
			visited.setBit(index, true);
			pending.enqueue(index);
		}
	};
	while (!pending.isEmpty()) {
		const int index = pending.dequeue();
		const int x = index % rasterSize.width();
		const int y = index / rasterSize.width();
		const QColor pixel = composite.pixelColor(x, y);
		if (qAbs(pixel.red() - target.red()) > 12 || qAbs(pixel.green() - target.green()) > 12
			|| qAbs(pixel.blue() - target.blue()) > 12)
			continue;
		fillImage.setPixelColor(x, y, QColor(currentColor_));
		left = qMin(left, x);
		right = qMax(right, x);
		top = qMin(top, y);
		bottom = qMax(bottom, y);
		enqueue(x - 1, y);
		enqueue(x + 1, y);
		enqueue(x, y - 1);
		enqueue(x, y + 1);
	}

	const QRect region(left, top, right - left + 1, bottom - top + 1);
	QByteArray pngData;
	QBuffer buffer(&pngData);
	if (!buffer.open(QIODevice::WriteOnly) || !fillImage.copy(region).save(&buffer, "PNG"))
		return;
	const QByteArray encoded = pngData.toBase64();
	if (encoded.size() > MaximumCanvasImageDataLength)
		return;
	CanvasObject fill;
	fill.id = newId();
	fill.layerId = activeLayerId_;
	fill.type = QStringLiteral("fill");
	fill.color = currentColor_;
	fill.fillColor = currentColor_;
	fill.imageData = QString::fromLatin1(encoded);
	fill.x = static_cast<double>(left) / rasterSize.width();
	fill.y = static_cast<double>(top) / rasterSize.height();
	fill.width = static_cast<double>(region.width()) / rasterSize.width();
	fill.height = static_cast<double>(region.height()) / rasterSize.height();
	fill.strokeWidth = strokeWidth_;
	fill.filled = true;
	emitObjectUpsert(fill);
}

void DrawingCanvas::pickColorAt(const QPointF& position)
{
	const qreal scale = qMin<qreal>(1.0,
		static_cast<qreal>(MaximumCanvasImageDimension) / qMax(width(), height()));
	const QSize rasterSize(qMax(1, qRound(width() * scale)), qMax(1, qRound(height() * scale)));
	QImage layerImage(rasterSize, QImage::Format_ARGB32_Premultiplied);
	layerImage.fill(Qt::transparent);
	QPainter painter(&layerImage);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.scale(static_cast<qreal>(rasterSize.width()) / qMax(1, width()),
		static_cast<qreal>(rasterSize.height()) / qMax(1, height()));
	for (const CanvasObject& object : objects_) {
		if (object.layerId == activeLayerId_)
			paintObject(painter, object);
	}
	painter.end();
	const int x = qBound(0, qRound(position.x() * (rasterSize.width() - 1)), rasterSize.width() - 1);
	const int y = qBound(0, qRound(position.y() * (rasterSize.height() - 1)), rasterSize.height() - 1);
	const QColor sampled = layerImage.pixelColor(x, y);
	if (sampled.alpha() > 0)
		setCurrentColor(sampled.name(QColor::HexRgb));
}

void DrawingCanvas::setCurrentColor(const QString& color)
{
	if (QColor(color).isValid() && currentColor_ != color) {
		currentColor_ = color;
		emit currentColorChanged(currentColor_);
	}
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

bool DrawingCanvas::canUndo() const
{
	return !undoHistory_.isEmpty();
}

bool DrawingCanvas::canRedo() const
{
	return !redoHistory_.isEmpty();
}

void DrawingCanvas::beginHistoryTransaction()
{
	if (historyTransactionActive_)
		return;
	pendingHistoryBefore_.clear();
	historyTransactionActive_ = true;
}

void DrawingCanvas::recordHistoryObject(const QString& objectId)
{
	if (!historyTransactionActive_ || pendingHistoryBefore_.contains(objectId))
		return;
	HistoryObjectState state;
	if (const CanvasObject* object = findObject(objectId)) {
		state.exists = true;
		state.object = *object;
	}
	pendingHistoryBefore_.insert(objectId, state);
}

void DrawingCanvas::endHistoryTransaction()
{
	if (!historyTransactionActive_)
		return;
	historyTransactionActive_ = false;
	HistoryEntry entry;
	for (auto iterator = pendingHistoryBefore_.cbegin(); iterator != pendingHistoryBefore_.cend(); ++iterator) {
		HistoryObjectState after;
		if (const CanvasObject* object = findObject(iterator.key())) {
			after.exists = true;
			after.object = *object;
		}
		const HistoryObjectState& before = iterator.value();
		if (before.exists == after.exists
			&& (!before.exists || before.object.toJson() == after.object.toJson()))
			continue;
		entry.before.insert(iterator.key(), before);
		entry.after.insert(iterator.key(), after);
	}
	pendingHistoryBefore_.clear();
	if (entry.before.isEmpty())
		return;
	undoHistory_.append(entry);
	if (undoHistory_.size() > 50)
		undoHistory_.removeFirst();
	redoHistory_.clear();
	emitHistoryChanged();
}

void DrawingCanvas::cancelHistoryTransaction()
{
	historyTransactionActive_ = false;
	pendingHistoryBefore_.clear();
}

void DrawingCanvas::restoreHistoryObjects(const QHash<QString, HistoryObjectState>& target)
{
	QStringList objectIds = target.keys();
	std::sort(objectIds.begin(), objectIds.end());
	for (const QString& id : objectIds) {
		if (!target.value(id).exists && findObject(id))
			emitOperation({{QStringLiteral("action"), QStringLiteral("object_delete")},
						   {QStringLiteral("objectId"), id}});
	}
	for (const QString& id : objectIds) {
		const HistoryObjectState& state = target.value(id);
		if (state.exists)
			emitObjectUpsert(state.object);
	}
}

void DrawingCanvas::undo()
{
	if (!drawingAllowed_ || undoHistory_.isEmpty())
		return;
	const HistoryEntry entry = undoHistory_.takeLast();
	historyReplay_ = true;
	restoreHistoryObjects(entry.before);
	historyReplay_ = false;
	redoHistory_.append(entry);
	emitHistoryChanged();
}

void DrawingCanvas::redo()
{
	if (!drawingAllowed_ || redoHistory_.isEmpty())
		return;
	const HistoryEntry entry = redoHistory_.takeLast();
	historyReplay_ = true;
	restoreHistoryObjects(entry.after);
	historyReplay_ = false;
	undoHistory_.append(entry);
	emitHistoryChanged();
}

void DrawingCanvas::clearHistory()
{
	undoHistory_.clear();
	redoHistory_.clear();
	cancelHistoryTransaction();
	emitHistoryChanged();
}

void DrawingCanvas::emitHistoryChanged()
{
	emit historyChanged(canUndo(), canRedo());
}

void DrawingCanvas::setScene(const QJsonArray& layers, const QJsonArray& objects)
{
	clearHistory();
	previewObject_ = CanvasObject{};
	polygonInProgress_ = false;
	dragMode_ = DragMode::None;
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
	if (!historyReplay_ && action != QStringLiteral("object_upsert")
		&& action != QStringLiteral("object_delete"))
		clearHistory();
	if (action == QStringLiteral("clear")) {
		objects_.clear();
		selectedObjectId_.clear();
		dragMode_ = DragMode::None;
		previewObject_ = CanvasObject{};
		polygonInProgress_ = false;
	} else if (action == QStringLiteral("clear_layer")) {
		const QString layerId = operation.value(QStringLiteral("layerId")).toString();
		objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&layerId](const CanvasObject& item) {
			return item.layerId == layerId;
		}), objects_.end());
		const CanvasObject* selected = findObject(selectedObjectId_);
		if (!selected || selected->layerId == layerId) {
			selectedObjectId_.clear();
			polygonInProgress_ = false;
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
	if (polygonInProgress_)
		cancelHistoryTransaction();
	else
		endHistoryTransaction();
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
	if (!historyReplay_) {
		const QString action = operation.value(QStringLiteral("action")).toString();
		if (action == QStringLiteral("object_upsert")) {
			const CanvasObject object = CanvasObject::fromJson(operation.value(QStringLiteral("object")).toObject());
			const bool ownTransaction = !historyTransactionActive_;
			if (ownTransaction)
				beginHistoryTransaction();
			recordHistoryObject(object.id);
			applyOperation(operation);
			emit operationGenerated(operation);
			if (ownTransaction)
				endHistoryTransaction();
			return;
		}
		if (action == QStringLiteral("object_delete")) {
			const QString objectId = operation.value(QStringLiteral("objectId")).toString();
			const bool ownTransaction = !historyTransactionActive_;
			if (ownTransaction)
				beginHistoryTransaction();
			recordHistoryObject(objectId);
			applyOperation(operation);
			emit operationGenerated(operation);
			if (ownTransaction)
				endHistoryTransaction();
			return;
		}
	}
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

void DrawingCanvas::finishPolygon(const QPointF& position, bool appendPoint)
{
	if (!polygonInProgress_)
		return;
	if (appendPoint && previewObject_.points.size() < MaximumObjectPoints) {
		const QPointF point = normalizedPoint(position);
		const QPointF last = previewObject_.points.isEmpty()
			? QPointF(-1.0, -1.0) : pointFromJson(previewObject_.points.last());
		if (previewObject_.points.isEmpty() || QLineF(last, point).length() > 0.002)
			previewObject_.points.append(pointJson(point));
	}
	if (previewObject_.points.size() >= 3) {
		updateBoundsFromPoints(&previewObject_);
		emitObjectUpsert(previewObject_);
		endHistoryTransaction();
	} else {
		cancelHistoryTransaction();
	}
	polygonInProgress_ = false;
	previewObject_ = CanvasObject{};
	update();
}

void DrawingCanvas::paintObject(QPainter& painter, const CanvasObject& object) const
{
	QColor color(object.color);
	if (!color.isValid())
		color = QColor(QStringLiteral("#202124"));
	const Qt::PenCapStyle cap = object.type == QStringLiteral("freehand") ? Qt::FlatCap : Qt::RoundCap;
	QPen pen(color, qMax<qreal>(1.0, object.strokeWidth * qMin(width(), height()) / 600.0),
			 Qt::SolidLine, cap, Qt::RoundJoin);
	painter.setPen(pen);
	painter.setBrush(object.filled ? QBrush(color) : Qt::NoBrush);
	const QRectF bounds = objectRect(object);
	if (object.type == QStringLiteral("rectangle")) {
		painter.drawRect(bounds);
	} else if (object.type == QStringLiteral("rounded_rectangle")) {
		const qreal radius = qMin<qreal>(16.0, qMin(bounds.width(), bounds.height()) / 4.0);
		painter.drawRoundedRect(bounds, radius, radius);
	} else if (object.type == QStringLiteral("ellipse")) {
		painter.drawEllipse(bounds);
	} else if (object.type == QStringLiteral("line")) {
		if (object.points.size() >= 2)
			painter.drawLine(pointFromJson(object.points.at(0)).x() * width(),
							 pointFromJson(object.points.at(0)).y() * height(),
							 pointFromJson(object.points.at(1)).x() * width(),
							 pointFromJson(object.points.at(1)).y() * height());
	} else if (object.type == QStringLiteral("freehand") || object.type == QStringLiteral("brush")
		|| object.type == QStringLiteral("airbrush") || object.type == QStringLiteral("curve")) {
		if (object.points.isEmpty())
			return;
		QPainterPath path;
		const QPointF first = pointFromJson(object.points.first());
		path.moveTo(first.x() * width(), first.y() * height());
		QPointF last = first;
		if (object.type == QStringLiteral("curve") && object.points.size() > 2) {
			for (qsizetype index = 1; index + 1 < object.points.size(); ++index) {
				const QPointF control = pointFromJson(object.points.at(index));
				const QPointF next = pointFromJson(object.points.at(index + 1));
				const QPointF midpoint = (control + next) / 2.0;
				path.quadTo(control.x() * width(), control.y() * height(),
							midpoint.x() * width(), midpoint.y() * height());
			}
			last = pointFromJson(object.points.last());
			path.lineTo(last.x() * width(), last.y() * height());
		} else {
			for (qsizetype index = 1; index < object.points.size(); ++index) {
				last = pointFromJson(object.points.at(index));
				path.lineTo(last.x() * width(), last.y() * height());
			}
		}
		if (object.type == QStringLiteral("airbrush")) {
			const qreal radius = qMax<qreal>(2.0, pen.widthF() * 2.0);
			const int particlesPerPoint = qBound(8, object.strokeWidth, 32);
			quint32 randomState = stableSeed(object.id);
			painter.save();
			painter.setPen(Qt::NoPen);
			for (const QJsonValue& value : object.points) {
				const QPointF center = pointFromJson(value);
				for (int index = 0; index < particlesPerPoint; ++index) {
					const qreal angle = (nextRandom(&randomState) / 4294967296.0) * 6.283185307179586;
					const qreal distance = std::sqrt(nextRandom(&randomState) / 4294967296.0) * radius;
					const qreal dotSize = 0.5 + (nextRandom(&randomState) / 4294967296.0) * 1.2;
					QColor dotColor = color;
					dotColor.setAlpha(55 + static_cast<int>(nextRandom(&randomState) % 100));
					painter.setBrush(dotColor);
					const QPointF centerPixels(center.x() * width() + std::cos(angle) * distance,
									   center.y() * height() + std::sin(angle) * distance);
					painter.drawEllipse(centerPixels, dotSize, dotSize);
				}
			}
			painter.restore();
		} else {
			painter.drawPath(path);
			if (object.points.size() == 1)
				painter.drawPoint(last.x() * width(), last.y() * height());
		}
	} else if (object.type == QStringLiteral("polygon")) {
		if (object.points.isEmpty())
			return;
		QPainterPath polygon;
		const QPointF first = pointFromJson(object.points.first());
		polygon.moveTo(first.x() * width(), first.y() * height());
		for (qsizetype index = 1; index < object.points.size(); ++index) {
			const QPointF point = pointFromJson(object.points.at(index));
			polygon.lineTo(point.x() * width(), point.y() * height());
		}
		if (object.points.size() >= 3)
			polygon.closeSubpath();
		painter.drawPath(polygon);
	} else if (object.type == QStringLiteral("fill")) {
		const QImage fillImage = QImage::fromData(QByteArray::fromBase64(object.imageData.toLatin1()), "PNG");
		if (!fillImage.isNull())
			painter.drawImage(bounds, fillImage);
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
	paintScene(painter);
	if (!previewObject_.id.isEmpty()) {
		if (polygonInProgress_) {
			CanvasObject preview = previewObject_;
			preview.points.append(pointJson(polygonCursor_));
			paintObject(painter, preview);
		} else {
			paintObject(painter, previewObject_);
		}
	}

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

QImage DrawingCanvas::renderImage() const
{
	if (size().isEmpty())
		return {};
	QImage image(size(), QImage::Format_RGB32);
	image.fill(Qt::white);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	paintScene(painter);
	return image;
}

void DrawingCanvas::paintScene(QPainter& painter) const
{
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
			beginHistoryTransaction();
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
		beginHistoryTransaction();
		dragMode_ = DragMode::Erase;
		dragStart_ = point;
		eraseSweep(point, point);
		event->accept();
		return;
	}
	if (tool_ == Tool::ObjectEraser) {
		beginHistoryTransaction();
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
	if (tool_ == Tool::Fill) {
		fillAt(point);
		event->accept();
		return;
	}
	if (tool_ == Tool::PickColor) {
		pickColorAt(point);
		event->accept();
		return;
	}
	if (tool_ == Tool::Polygon) {
		if (!polygonInProgress_) {
			beginHistoryTransaction();
			previewObject_ = CanvasObject{};
			previewObject_.id = newId();
			previewObject_.layerId = activeLayerId_;
			previewObject_.type = QStringLiteral("polygon");
			previewObject_.color = currentColor_;
			previewObject_.strokeWidth = strokeWidth_;
			previewObject_.filled = filled_;
			polygonInProgress_ = true;
		} else if (previewObject_.points.size() >= 3) {
			const QPointF first = pointFromJson(previewObject_.points.first());
			if (QLineF(first, point).length() < 0.02) {
				finishPolygon(point, false);
				event->accept();
				return;
			}
		}
		if (previewObject_.points.size() < MaximumObjectPoints)
			previewObject_.points.append(pointJson(point));
		updateBoundsFromPoints(&previewObject_);
		polygonCursor_ = point;
		update();
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
		: tool_ == Tool::Brush ? QStringLiteral("brush")
		: tool_ == Tool::Airbrush ? QStringLiteral("airbrush")
		: tool_ == Tool::Curve ? QStringLiteral("curve")
		: tool_ == Tool::Rectangle ? QStringLiteral("rectangle")
		: tool_ == Tool::RoundedRectangle ? QStringLiteral("rounded_rectangle")
		: tool_ == Tool::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("line");
	beginHistoryTransaction();
	if (tool_ == Tool::Pen || tool_ == Tool::Brush || tool_ == Tool::Airbrush || tool_ == Tool::Curve) {
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
	if (polygonInProgress_) {
		polygonCursor_ = normalizedPoint(event->position());
		update();
	} else if (dragMode_ == DragMode::Move || dragMode_ == DragMode::Resize) {
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
		if (tool_ == Tool::Pen || tool_ == Tool::Brush || tool_ == Tool::Airbrush || tool_ == Tool::Curve) {
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
		const bool pathTool = tool_ == Tool::Pen || tool_ == Tool::Brush
			|| tool_ == Tool::Airbrush || tool_ == Tool::Curve;
		if (dragMode_ == DragMode::Draw && !pathTool) {
			if (tool_ == Tool::Line)
				updateBoundsFromPoints(&previewObject_);
			if (previewObject_.width > 0.001 || previewObject_.height > 0.001)
				emitObjectUpsert(previewObject_);
			previewObject_ = CanvasObject{};
		}
		if (dragMode_ == DragMode::Draw && pathTool)
			selectedObjectId_.clear();
		if (dragMode_ == DragMode::Draw || dragMode_ == DragMode::Erase
			|| dragMode_ == DragMode::Move || dragMode_ == DragMode::Resize)
			endHistoryTransaction();
		dragMode_ = DragMode::None;
		resizeHandle_ = -1;
		update();
	}
	QWidget::mouseReleaseEvent(event);
}

void DrawingCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (drawingAllowed_ && tool_ == Tool::Polygon && event->button() == Qt::LeftButton) {
		finishPolygon(event->position(), true);
		event->accept();
		return;
	}
	QWidget::mouseDoubleClickEvent(event);
}

}
