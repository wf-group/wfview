#include "cwmorse.h"

#include <QtTest>

class CwMorseTest : public QObject
{
    Q_OBJECT

private slots:
    void letterEndsWithCharacterGap();
    void dashUsesRatio();
    void spaceMakesWordGap();
    void unknownCharacterIsQuestionMark();
};

namespace
{
double totalDits(const std::deque<CwElement> &elements)
{
    double dits = 0.0;
    for (const CwElement &element : elements)
        dits += element.dits;
    return dits;
}
}

void CwMorseTest::letterEndsWithCharacterGap()
{
    // E is one dit, followed by the three-dit gap between characters.
    std::deque<CwElement> elements;
    appendCwElements('E', 3.0, elements);
    QCOMPARE(int(elements.size()), 2);
    QCOMPARE(elements[0].keyDown, true);
    QCOMPARE(elements[0].dits, 1.0);
    QCOMPARE(elements[1].keyDown, false);
    QCOMPARE(elements[1].dits, 3.0);
}

void CwMorseTest::dashUsesRatio()
{
    // N is dash, gap, dit, character gap.
    std::deque<CwElement> elements;
    appendCwElements('N', 3.5, elements);
    QCOMPARE(int(elements.size()), 4);
    QCOMPARE(elements[0].keyDown, true);
    QCOMPARE(elements[0].dits, 3.5);
    QCOMPARE(elements[1].keyDown, false);
    QCOMPARE(elements[1].dits, 1.0);
    QCOMPARE(elements[2].keyDown, true);
    QCOMPARE(elements[2].dits, 1.0);
    QCOMPARE(elements[3].dits, 3.0);
}

void CwMorseTest::spaceMakesWordGap()
{
    // "E E" is 1 + 7 + 1 dits, plus the gap after the last character.
    std::deque<CwElement> elements;
    for (const QChar ch : QStringLiteral("E E"))
        appendCwElements(ch, 3.0, elements);
    QCOMPARE(totalDits(elements), 1.0 + 7.0 + 1.0 + 3.0);
    for (size_t i = 1; i + 2 < elements.size(); ++i)
        QCOMPARE(elements[i].keyDown, false);
}

void CwMorseTest::unknownCharacterIsQuestionMark()
{
    std::deque<CwElement> unknown;
    std::deque<CwElement> question;
    appendCwElements('}', 3.0, unknown);
    appendCwElements('?', 3.0, question);
    QCOMPARE(unknown.size(), question.size());
    QCOMPARE(totalDits(unknown), totalDits(question));
}

int runCwMorseTests(int argc, char **argv)
{
    CwMorseTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_cwmorse.moc"
