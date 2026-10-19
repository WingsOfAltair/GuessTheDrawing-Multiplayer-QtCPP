#include "shared/models.h"

#include <QtTest>

class CanvasModelTests final : public QObject {
	Q_OBJECT

private slots:
	void roundTripsCanvasObjectsAndLayers()
	{
		gtd::CanvasLayer layer{QStringLiteral("layer-a"), QStringLiteral("Sketch"), 2, true, false};
		const gtd::CanvasLayer decodedLayer = gtd::CanvasLayer::fromJson(layer.toJson());
		QCOMPARE(decodedLayer.id, layer.id);
		QCOMPARE(decodedLayer.name, layer.name);
		QCOMPARE(decodedLayer.order, layer.order);

		gtd::CanvasObject item;
		item.id = QStringLiteral("object-a");
		item.layerId = layer.id;
		item.type = QStringLiteral("rectangle");
		item.color = QStringLiteral("#123456");
		item.x = 0.1;
		item.y = 0.2;
		item.width = 0.3;
		item.height = 0.4;
		item.filled = true;
		const gtd::CanvasObject decodedItem = gtd::CanvasObject::fromJson(item.toJson());
		QVERIFY(gtd::isValidCanvasObject(decodedItem));
		QCOMPARE(decodedItem.id, item.id);
		QCOMPARE(decodedItem.layerId, item.layerId);
		QCOMPARE(decodedItem.type, item.type);
		QCOMPARE(decodedItem.color, item.color);
		QCOMPARE(decodedItem.x, item.x);
		QCOMPARE(decodedItem.width, item.width);
		QVERIFY(decodedItem.filled);
	}

	void validatesNormalizedGeometryAndColors()
	{
		gtd::CanvasObject item;
		item.id = QStringLiteral("object-a");
		item.layerId = QStringLiteral("layer-a");
		item.type = QStringLiteral("ellipse");
		item.x = 0.2;
		item.y = 0.3;
		item.width = 0.4;
		item.height = 0.5;
		QVERIFY(gtd::isValidCanvasObject(item));

		item.width = 0.9;
		QVERIFY(!gtd::isValidCanvasObject(item));
		item.width = 0.4;
		item.color = QStringLiteral("not-a-color");
		QVERIFY(!gtd::isValidCanvasObject(item));
	}

	void validatesLayerAndPathLimits()
	{
		gtd::CanvasLayer layer{QStringLiteral("layer-a"), QStringLiteral("Visible layer"), 0, true, false};
		QVERIFY(gtd::isValidCanvasLayer(layer));
		layer.name = QStringLiteral("Hidden\nLayer");
		QVERIFY(!gtd::isValidCanvasLayer(layer));
		layer.name = QStringLiteral("Visible layer");
		layer.order = gtd::MaximumCanvasLayers;
		QVERIFY(!gtd::isValidCanvasLayer(layer));

		gtd::CanvasObject path;
		path.id = QStringLiteral("path-a");
		path.layerId = QStringLiteral("layer-a");
		path.type = QStringLiteral("freehand");
		path.points.append(QJsonObject{{QStringLiteral("x"), 0.2}, {QStringLiteral("y"), 0.3}});
		QVERIFY(gtd::isValidCanvasObject(path));
		path.points[0] = QJsonObject{{QStringLiteral("x"), 1.2}, {QStringLiteral("y"), 0.3}};
		QVERIFY(!gtd::isValidCanvasObject(path));
	}

	void validatesNewDrawingObjectTypesAndFillPayloads()
	{
		const QJsonArray strokePoints{
			QJsonObject{{QStringLiteral("x"), 0.2}, {QStringLiteral("y"), 0.3}},
			QJsonObject{{QStringLiteral("x"), 0.8}, {QStringLiteral("y"), 0.7}}};
		for (const QString& type : {QStringLiteral("brush"), QStringLiteral("airbrush"), QStringLiteral("curve")}) {
			gtd::CanvasObject stroke;
			stroke.id = QStringLiteral("stroke-a");
			stroke.layerId = QStringLiteral("layer-a");
			stroke.type = type;
			stroke.points = strokePoints;
			QVERIFY2(gtd::isValidCanvasObject(stroke), qPrintable(type));
		}

		gtd::CanvasObject polygon;
		polygon.id = QStringLiteral("polygon-a");
		polygon.layerId = QStringLiteral("layer-a");
		polygon.type = QStringLiteral("polygon");
		polygon.points = QJsonArray{strokePoints.first(), strokePoints.last(),
			QJsonObject{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.9}}};
		QVERIFY(gtd::isValidCanvasObject(polygon));

		gtd::CanvasObject roundedRectangle;
		roundedRectangle.id = QStringLiteral("rounded-a");
		roundedRectangle.layerId = QStringLiteral("layer-a");
		roundedRectangle.type = QStringLiteral("rounded_rectangle");
		QVERIFY(gtd::isValidCanvasObject(roundedRectangle));

		gtd::CanvasObject fill;
		fill.id = QStringLiteral("fill-a");
		fill.layerId = QStringLiteral("layer-a");
		fill.type = QStringLiteral("fill");
		fill.imageData = QStringLiteral("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aGZsAAAAASUVORK5CYII=");
		const gtd::CanvasObject decodedFill = gtd::CanvasObject::fromJson(fill.toJson());
		QVERIFY(gtd::isValidCanvasObject(decodedFill));
		QCOMPARE(decodedFill.imageData, fill.imageData);
		fill.imageData = QStringLiteral("not-an-image");
		QVERIFY(!gtd::isValidCanvasObject(fill));
	}
};

QTEST_GUILESS_MAIN(CanvasModelTests)
#include "test_canvas_models.moc"
