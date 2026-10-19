#include "client/drawing_canvas.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class DrawingCanvasTests final : public QObject {
	Q_OBJECT

private slots:
	void selectToolHandlesPointerMovementOverEmptyCanvas()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);
		canvas.setTool(gtd::DrawingCanvas::Tool::Select);
		canvas.show();

		QTest::mouseMove(&canvas, QPoint(50, 50));
	}

	void multilineTextAllocatesVerticalSpace()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(400, 200);
		canvas.setDrawingAllowed(true);
		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);

		canvas.addTextAt(QPointF(0.1, 0.1), QStringLiteral("First line"));
		QCOMPARE(operations.size(), 1);
		const QJsonObject singleLineObject = operations.first().first().toJsonObject()
			.value(QStringLiteral("object")).toObject();
		const double singleLineHeight = singleLineObject.value(QStringLiteral("height")).toDouble();

		operations.clear();
		canvas.addTextAt(QPointF(0.1, 0.1), QStringLiteral("First line\nSecond line\nThird line"));
		QCOMPARE(operations.size(), 1);
		const QJsonObject multilineObject = operations.first().first().toJsonObject()
			.value(QStringLiteral("object")).toObject();
		QVERIFY(multilineObject.value(QStringLiteral("height")).toDouble() > singleLineHeight * 2.0);
	}

	void movingLayerImmediatelyChangesRenderedStackingAndSyncsAtomically()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);

		gtd::CanvasObject bottom = rectangleObject(QStringLiteral("bottom-shape"), QStringLiteral("layer-bottom"));
		bottom.color = QStringLiteral("#ff0000");
		bottom.filled = true;
		gtd::CanvasObject top = rectangleObject(QStringLiteral("top-shape"), QStringLiteral("layer-top"));
		top.color = QStringLiteral("#0000ff");
		top.filled = true;
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-bottom"), QStringLiteral("Bottom"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-top"), QStringLiteral("Top"), 1, true, false}.toJson()},
			QJsonArray{bottom.toJson(), top.toJson()});
		canvas.setSelectedLayer(QStringLiteral("layer-bottom"));
		canvas.show();
		QCoreApplication::processEvents();

		QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
		canvas.render(&image);
		QCOMPARE(image.pixelColor(50, 50), QColor(QStringLiteral("#0000ff")));

		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);
		canvas.moveSelectedLayer(1);
		QCOMPARE(operations.size(), 1);
		const QJsonObject reorder = operations.first().first().toJsonObject();
		QCOMPARE(reorder.value(QStringLiteral("action")).toString(), QStringLiteral("layer_reorder"));
		const QJsonArray expectedLayerIds{QStringLiteral("layer-top"), QStringLiteral("layer-bottom")};
		QCOMPARE(reorder.value(QStringLiteral("layerIds")).toArray(), expectedLayerIds);
		QCOMPARE(canvas.layers().at(0).id, QStringLiteral("layer-top"));
		QCOMPARE(canvas.layers().at(0).order, 0);
		QCOMPARE(canvas.layers().at(1).id, QStringLiteral("layer-bottom"));
		QCOMPARE(canvas.layers().at(1).order, 1);

		QCoreApplication::processEvents();
		image.fill(Qt::transparent);
		canvas.render(&image);
		QCOMPARE(image.pixelColor(50, 50), QColor(QStringLiteral("#ff0000")));

		gtd::DrawingCanvas remoteCanvas;
		remoteCanvas.setMinimumSize(0, 0);
		remoteCanvas.resize(100, 100);
		remoteCanvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-bottom"), QStringLiteral("Bottom"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-top"), QStringLiteral("Top"), 1, true, false}.toJson()},
			QJsonArray{bottom.toJson(), top.toJson()});
		remoteCanvas.applyOperation(reorder);
		remoteCanvas.show();
		QCoreApplication::processEvents();
		QImage remoteImage(remoteCanvas.size(), QImage::Format_ARGB32_Premultiplied);
		remoteCanvas.render(&remoteImage);
		QCOMPARE(remoteImage.pixelColor(50, 50), QColor(QStringLiteral("#ff0000")));

		canvas.moveSelectedLayer(-1);
		QCOMPARE(operations.size(), 2);
		QCoreApplication::processEvents();
		image.fill(Qt::transparent);
		canvas.render(&image);
		QCOMPARE(image.pixelColor(50, 50), QColor(QStringLiteral("#0000ff")));
	}

	void movingLayerChangesFreehandStrokeCompositing()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);

		gtd::CanvasObject redStroke = freehandObject(QStringLiteral("red-stroke"), QStringLiteral("layer-red"));
		redStroke.color = QStringLiteral("#ff0000");
		redStroke.strokeWidth = 32;
		gtd::CanvasObject blackStroke = freehandObject(QStringLiteral("black-stroke"), QStringLiteral("layer-black"));
		blackStroke.color = QStringLiteral("#000000");
		blackStroke.strokeWidth = 32;
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-red"), QStringLiteral("Red"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-black"), QStringLiteral("Black"), 1, true, false}.toJson()},
			QJsonArray{redStroke.toJson(), blackStroke.toJson()});
		canvas.setSelectedLayer(QStringLiteral("layer-black"));
		canvas.show();
		QCoreApplication::processEvents();

		gtd::DrawingCanvas remoteCanvas;
		remoteCanvas.setMinimumSize(0, 0);
		remoteCanvas.resize(100, 100);
		remoteCanvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-red"), QStringLiteral("Red"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-black"), QStringLiteral("Black"), 1, true, false}.toJson()},
			QJsonArray{redStroke.toJson(), blackStroke.toJson()});
		remoteCanvas.show();

		QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
		canvas.render(&image);
		QCOMPARE(image.pixelColor(50, 50), QColor(QStringLiteral("#000000")));
		QImage remoteImage(remoteCanvas.size(), QImage::Format_ARGB32_Premultiplied);
		remoteCanvas.render(&remoteImage);
		QCOMPARE(remoteImage.pixelColor(50, 50), QColor(QStringLiteral("#000000")));

		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);
		canvas.moveSelectedLayer(-1);
		QCOMPARE(operations.size(), 1);
		const QJsonArray expectedOrder{QStringLiteral("layer-black"), QStringLiteral("layer-red")};
		QCOMPARE(operations.first().first().toJsonObject().value(QStringLiteral("layerIds")).toArray(),
				 expectedOrder);
		QCoreApplication::processEvents();
		image.fill(Qt::transparent);
		canvas.render(&image);
		QCOMPARE(image.pixelColor(50, 50), QColor(QStringLiteral("#ff0000")));
		remoteCanvas.applyOperation(operations.first().first().toJsonObject());
		remoteImage.fill(Qt::transparent);
		remoteCanvas.render(&remoteImage);
		QCOMPARE(remoteImage.pixelColor(50, 50), QColor(QStringLiteral("#ff0000")));

		canvas.moveSelectedLayer(1);
		QCOMPARE(operations.size(), 2);
		QCoreApplication::processEvents();
		image.fill(Qt::transparent);
		canvas.render(&image);
		QCOMPARE(image.pixelColor(50, 50), QColor(QStringLiteral("#000000")));
		remoteCanvas.applyOperation(operations.last().first().toJsonObject());
		remoteImage.fill(Qt::transparent);
		remoteCanvas.render(&remoteImage);
		QCOMPARE(remoteImage.pixelColor(50, 50), QColor(QStringLiteral("#000000")));
	}

	void freehandEraserOnlyChangesSelectedLayer()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-a"), QStringLiteral("A"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-b"), QStringLiteral("B"), 1, true, false}.toJson()},
			QJsonArray{freehandObject(QStringLiteral("stroke-b"), QStringLiteral("layer-b")).toJson()});
		canvas.setSelectedLayer(QStringLiteral("layer-a"));
		canvas.setTool(gtd::DrawingCanvas::Tool::Eraser);
		canvas.show();
		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);

		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QCOMPARE(operations.size(), 0);

		canvas.setSelectedLayer(QStringLiteral("layer-b"));
		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QVERIFY(!operations.isEmpty());
	}

	void objectEraserOnlyChangesSelectedLayer()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-a"), QStringLiteral("A"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-b"), QStringLiteral("B"), 1, true, false}.toJson()},
			QJsonArray{rectangleObject(QStringLiteral("shape-b"), QStringLiteral("layer-b")).toJson()});
		canvas.setSelectedLayer(QStringLiteral("layer-a"));
		canvas.setTool(gtd::DrawingCanvas::Tool::ObjectEraser);
		canvas.show();
		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);

		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QCOMPARE(operations.size(), 0);

		canvas.setSelectedLayer(QStringLiteral("layer-b"));
		QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QCOMPARE(operations.size(), 1);
		QCOMPARE(operations.first().first().toJsonObject().value(QStringLiteral("action")).toString(),
				 QStringLiteral("object_delete"));
	}

	void selectToolCannotTargetAnotherLayer()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-a"), QStringLiteral("A"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-b"), QStringLiteral("B"), 1, true, false}.toJson()},
			QJsonArray{rectangleObject(QStringLiteral("shape-b"), QStringLiteral("layer-b")).toJson()});
		canvas.setSelectedLayer(QStringLiteral("layer-a"));
		canvas.setTool(gtd::DrawingCanvas::Tool::Select);
		canvas.show();
		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);

		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QTest::mouseMove(&canvas, QPoint(60, 60));
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));

		QCOMPARE(operations.size(), 0);
		QCOMPARE(canvas.selectedLayerId(), QStringLiteral("layer-a"));
	}

	void clearSelectedLayerPreservesOtherLayers()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-a"), QStringLiteral("A"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-b"), QStringLiteral("B"), 1, true, false}.toJson()},
			QJsonArray{rectangleObject(QStringLiteral("shape-a"), QStringLiteral("layer-a")).toJson(),
				   rectangleObject(QStringLiteral("shape-b"), QStringLiteral("layer-b")).toJson()});
		canvas.show();
		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);

		canvas.clearSelectedLayer();
		QCOMPARE(operations.size(), 1);
		QCOMPARE(operations.first().first().toJsonObject().value(QStringLiteral("action")).toString(),
				 QStringLiteral("clear_layer"));
		QCOMPARE(operations.first().first().toJsonObject().value(QStringLiteral("layerId")).toString(),
				 QStringLiteral("layer-a"));
		operations.clear();
		canvas.setTool(gtd::DrawingCanvas::Tool::Select);
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QTest::mouseMove(&canvas, QPoint(60, 60));
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
		QCOMPARE(operations.size(), 0);

		canvas.setSelectedLayer(QStringLiteral("layer-b"));
		QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
		QTest::mouseMove(&canvas, QPoint(60, 60));
		QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
		QCOMPARE(operations.size(), 1);
		QCOMPARE(operations.first().first().toJsonObject().value(QStringLiteral("action")).toString(),
				 QStringLiteral("object_upsert"));
	}

	void clearCanvasRemovesObjectsFromAllLayers()
	{
		gtd::DrawingCanvas canvas;
		canvas.setMinimumSize(0, 0);
		canvas.resize(100, 100);
		canvas.setDrawingAllowed(true);
		canvas.setScene(
			QJsonArray{gtd::CanvasLayer{QStringLiteral("layer-a"), QStringLiteral("A"), 0, true, false}.toJson(),
					   gtd::CanvasLayer{QStringLiteral("layer-b"), QStringLiteral("B"), 1, true, false}.toJson()},
			QJsonArray{rectangleObject(QStringLiteral("shape-a"), QStringLiteral("layer-a")).toJson(),
				   rectangleObject(QStringLiteral("shape-b"), QStringLiteral("layer-b")).toJson()});
		canvas.show();
		QSignalSpy operations(&canvas, &gtd::DrawingCanvas::operationGenerated);

		canvas.clearCanvas();
		QCOMPARE(operations.size(), 1);
		QCOMPARE(operations.first().first().toJsonObject().value(QStringLiteral("action")).toString(),
				 QStringLiteral("clear"));
		operations.clear();
		canvas.setTool(gtd::DrawingCanvas::Tool::Select);
		for (const QString& layerId : {QStringLiteral("layer-a"), QStringLiteral("layer-b")}) {
			canvas.setSelectedLayer(layerId);
			QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
			QTest::mouseMove(&canvas, QPoint(60, 60));
			QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(60, 60));
		}
		QCOMPARE(operations.size(), 0);
	}

private:
	static gtd::CanvasObject freehandObject(const QString& id, const QString& layerId)
	{
		gtd::CanvasObject item;
		item.id = id;
		item.layerId = layerId;
		item.type = QStringLiteral("freehand");
		item.color = QStringLiteral("#202124");
		item.points = QJsonArray{QJsonObject{{QStringLiteral("x"), 0.2}, {QStringLiteral("y"), 0.5}},
								 QJsonObject{{QStringLiteral("x"), 0.8}, {QStringLiteral("y"), 0.5}}};
		item.x = 0.2;
		item.y = 0.5;
		item.width = 0.6;
		return item;
	}

	static gtd::CanvasObject rectangleObject(const QString& id, const QString& layerId)
	{
		gtd::CanvasObject item;
		item.id = id;
		item.layerId = layerId;
		item.type = QStringLiteral("rectangle");
		item.color = QStringLiteral("#202124");
		item.x = 0.4;
		item.y = 0.4;
		item.width = 0.2;
		item.height = 0.2;
		return item;
	}
};

QTEST_MAIN(DrawingCanvasTests)
#include "test_drawing_canvas.moc"
