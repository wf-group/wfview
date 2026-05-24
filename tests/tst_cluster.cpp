#include "cluster.h"

#include <QtTest>

class ClusterTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesTcpSpotLines_data();
    void parsesTcpSpotLines();
    void rejectsNonSpotLines();
};

void ClusterTest::parsesTcpSpotLines_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("spotter");
    QTest::addColumn<double>("frequency");
    QTest::addColumn<QString>("dxcall");
    QTest::addColumn<QString>("comment");

    QTest::newRow("dxspider")
        << QStringLiteral("DX de K1ABC: 14074.0 DX1ABC FT8 -10 dB 1234Z")
        << QStringLiteral("K1ABC") << 14.074 << QStringLiteral("DX1ABC") << QStringLiteral("FT8 -10 dB");
    QTest::newRow("ar-cluster")
        << QStringLiteral("DX de AD5YJ:    14222.2  K5NA      +W-TX-STX-Travis -4-7-Richard 1801Z")
        << QStringLiteral("AD5YJ") << 14.2222 << QStringLiteral("K5NA") << QStringLiteral("+W-TX-STX-Travis -4-7-Richard");
    QTest::newRow("cc-cluster-skimmer")
        << QStringLiteral("DX de KM3T-#: 3548.5 NZ0T CW 15 dB 20 WPM CQ 0055Z United States")
        << QStringLiteral("KM3T-#") << 3.5485 << QStringLiteral("NZ0T") << QStringLiteral("CW 15 dB 20 WPM CQ");
    QTest::newRow("slash-and-suffix")
        << QStringLiteral("DX de W3LPL-@: 18100.5 F/HB9ABC/P RTTY spotted in EU 0923Z")
        << QStringLiteral("W3LPL-@") << 18.1005 << QStringLiteral("F/HB9ABC/P") << QStringLiteral("RTTY spotted in EU");
}

void ClusterTest::parsesTcpSpotLines()
{
    QFETCH(QString, line);
    QFETCH(QString, spotter);
    QFETCH(double, frequency);
    QFETCH(QString, dxcall);
    QFETCH(QString, comment);

    spotData spot;
    QVERIFY(dxClusterClient::parseTcpSpotLine(line, 30, &spot));
    QCOMPARE(spot.spottercall, spotter);
    QCOMPARE(spot.frequency, frequency);
    QCOMPARE(spot.dxcall, dxcall);
    QCOMPARE(spot.comment, comment);
    QCOMPARE(spot.timeout, 30);
    QVERIFY(spot.timestamp.isValid());
}

void ClusterTest::rejectsNonSpotLines()
{
    spotData spot;
    QVERIFY(!dxClusterClient::parseTcpSpotLine(QStringLiteral("login:"), 30, &spot));
    QVERIFY(!dxClusterClient::parseTcpSpotLine(QStringLiteral("Welcome to AR-Cluster"), 30, &spot));
    QVERIFY(!dxClusterClient::parseTcpSpotLine(QStringLiteral("DX de K1ABC: no-frequency DX1ABC 1234Z"), 30, &spot));
}

int runClusterTests(int argc, char **argv)
{
    ClusterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_cluster.moc"
