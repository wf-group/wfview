#include "cwmorse.h"

#include <QHash>
#include <cstring>

namespace
{
const QHash<QChar, const char *> &cwTable()
{
    static const QHash<QChar, const char *> table = {
        {'0', "-----"},   {'1', ".----"},  {'2', "..---"},  {'3', "...--"},  {'4', "....-"},  {'5', "....."},
        {'6', "-...."},   {'7', "--..."},  {'8', "---.."},  {'9', "----."},  {'A', ".-"},     {'B', "-..."},
        {'C', "-.-."},    {'D', "-.."},    {'E', "."},      {'F', "..-."},   {'G', "--."},    {'H', "...."},
        {'I', ".."},      {'J', ".---"},   {'K', "-.-"},    {'L', ".-.."},   {'M', "--"},     {'N', "-."},
        {'O', "---"},     {'P', ".--."},   {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
        {'U', "..-"},     {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},   {'Y', "-.--"},   {'Z', "--.."},
        {'/', "-..-."},   {'?', "..--.."}, {'.', ".-.-.-"}, {'-', "-....-"}, {',', "--..--"}, {':', "---..."},
        {'\'', ".----."}, {'(', "-.--.-"}, {')', "-.--.-"}, {'=', "-...-"},  {'+', ".-.-."},  {'"', ".-..-."},
        {' ', " "},
    };
    return table;
}
}

void appendCwElements(QChar ch, double dashRatio, std::deque<CwElement> &elements)
{
    const auto &table = cwTable();
    auto it = table.constFind(ch);
    if (it == table.constEnd())
        it = table.constFind('?');

    const char *morse = it.value();
    const size_t length = std::strlen(morse);
    for (size_t i = 0; i < length; ++i)
    {
        const char symbol = morse[i];
        if (symbol == '-')
            elements.push_back({true, dashRatio});
        else if (symbol == '.')
            elements.push_back({true, 1.0});
        else
            elements.push_back({false, 1.0}); // word space

        // One dit between elements, three after the character.
        const bool last = (i == length - 1);
        elements.push_back({false, last ? 3.0 : 1.0});
    }
}
