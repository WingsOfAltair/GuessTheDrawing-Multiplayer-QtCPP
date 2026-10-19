#pragma once

#include "shared/models.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QPainter;

namespace gtd {

class DrawingCanvas final : public QWidget {
	Q_OBJECT

public:
	enum class Tool { Select, Pen, Brush, Airbrush, Curve, Polygon, Eraser, ObjectEraser,
					  Rectangle, RoundedRectangle, Ellipse, Line, Fill, PickColor, Text };

	explicit DrawingCanvas(QWidget* parent = nullptr);
	void setDrawingAllowed(bool allowed);
	void setTool(Tool tool);
	void setCurrentColor(const QString& color);
	void setStrokeWidth(int width);
	void setFontSize(int size);
	void setFilled(bool filled);
	bool canUndo() const;
	bool canRedo() const;
	void undo();
	void redo();
	void setScene(const QJsonArray& layers, const QJsonArray& objects);
	void applyOperation(const QJsonObject& operation);
	void setSelectedLayer(const QString& layerId);
	void addTextAt(const QPointF& position, const QString& text);
	void clearSelectedLayer();
	void clearCanvas();
	void addLayer(const QString& name);
	void deleteSelectedLayer();
	void renameSelectedLayer(const QString& name);
	void setSelectedLayerVisible(bool visible);
	void setSelectedLayerLocked(bool locked);
	void moveSelectedLayer(int offset);
	QList<CanvasLayer> layers() const;
	QString selectedLayerId() const;

signals:
	void operationGenerated(const QJsonObject& operation);
	void layersChanged(const QJsonArray& layers);
	void selectedLayerChanged(const QString& layerId);
	void textRequested(const QPointF& position);
	void currentColorChanged(const QString& color);
	void historyChanged(bool canUndo, bool canRedo);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	struct HistoryObjectState {
		bool exists = false;
		CanvasObject object;
	};
	struct HistoryEntry {
		QHash<QString, HistoryObjectState> before;
		QHash<QString, HistoryObjectState> after;
	};

	QPointF normalizedPoint(const QPointF& position) const;
	QRectF objectRect(const CanvasObject& object) const;
	int hitTest(const QPointF& point) const;
	int handleAt(const QRectF& bounds, const QPointF& point) const;
	void emitOperation(const QJsonObject& operation);
	void emitObjectUpsert(const CanvasObject& object);
	void emitLayerUpsert(const CanvasLayer& layer);
	void updateLayer(const CanvasLayer& layer);
	CanvasLayer* findLayer(const QString& layerId);
	const CanvasLayer* findLayer(const QString& layerId) const;
	CanvasObject* findObject(const QString& objectId);
	void updateDrag(const QPointF& position);
	void updateBoundsFromPoints(CanvasObject* object) const;
	void fillAt(const QPointF& position);
	void pickColorAt(const QPointF& position);
	void finishPolygon(const QPointF& position, bool appendPoint);
	void beginHistoryTransaction();
	void endHistoryTransaction();
	void cancelHistoryTransaction();
	void recordHistoryObject(const QString& objectId);
	void restoreHistoryObjects(const QHash<QString, HistoryObjectState>& objects);
	void clearHistory();
	void emitHistoryChanged();
	void eraseFreehandAt(const QPointF& center, qreal radiusPixels);
	void eraseSweep(const QPointF& from, const QPointF& to);
	void eraseObjectsAt(const QPointF& position);
	void paintObject(QPainter& painter, const CanvasObject& object) const;
	static QJsonObject upsertObjectOperation(const CanvasObject& object);

	enum class DragMode { None, Draw, Move, Resize, Erase };
	QList<CanvasLayer> layers_;
	QList<CanvasObject> objects_;
	QList<HistoryEntry> undoHistory_;
	QList<HistoryEntry> redoHistory_;
	QHash<QString, HistoryObjectState> pendingHistoryBefore_;
	CanvasObject previewObject_;
	CanvasObject originalObject_;
	QString activeLayerId_;
	QString selectedObjectId_;
	QPointF dragStart_;
	QPointF polygonCursor_;
	int resizeHandle_ = -1;
	DragMode dragMode_ = DragMode::None;
	Tool tool_ = Tool::Pen;
	QString currentColor_ = QStringLiteral("#202124");
	int strokeWidth_ = 4;
	int fontSize_ = 24;
	bool filled_ = false;
	bool drawingAllowed_ = false;
	bool polygonInProgress_ = false;
	bool historyTransactionActive_ = false;
	bool historyReplay_ = false;
};

}
