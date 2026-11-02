// cwwordjoiner.h
//
// Word joiner for the CW decoder's hand-key engine. Qt-free.
//
// Hand senders often leave a gap inside a word that is as long as another
// sender's word gap ("P OWER", "KG 9 H V", "IC 7 3 00" in real recordings). Timing
// alone cannot settle those, so the engine marks each word break as "soft"
// (gap not much longer than a letter gap) or "hard" (clearly a word gap), and
// this joiner decides which soft breaks to close by scoring the result:
// known ham / plain-language words, callsign shapes, RST reports and
// numbers score well; stray 1-2 letter fragments score badly.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

class CwWordJoiner
{
public:
    CwWordJoiner()
    {
        // clang-format off
        static const char *words[] = {
            // procedure, Q codes, abbreviations
            "CQ","DE","K","KN","SK","AR","BK","R","RR","TU","TNX","TKS","THX","FB","OM","YL","XYL",
            "GM","GA","GE","GN","GL","GD","HW","CPY","CPI","PSE","AGN","UR","URS","RST","ES","HR","HRD",
            "FER","FR","WX","TEMP","QTH","QRZ","QRM","QRN","QSB","QSO","QSL","QRP","QRO","QSY","QRL",
            "QRS","QRQ","QRT","QRV","QRX","QTR","QSX","OP","NAME","NR","SN","RIG","ANT","PWR","POWER",
            "WATTS","WATT","W","KW","AMP","KEY","BUG","PADDLE","PADDLES","STRAIGHT","SIDESWIPER",
            "VIBROPLEX","DIPOLE","VERT","VERTICAL","BEAM","YAGI","LOOP","WIRE","INV","INVERTED","VEE",
            "EFHW","WINDOM","DELTA","QUAD","TOWER","FT","FEET","UP","AT","ABOUT","ABT","HI","HIHI",
            "VY","VERY","NICE","GUD","GOOD","BEST","73","72","88","599","5NN","559","579","589","569",
            "449","339","229","CUL","CU","CUAGN","SRI","SORRY","OK","ALL","WL","WILL","WKD","WRK",
            "WORK","DX","CONDX","CONDITIONS","BAND","BANDS","MHZ","KHZ","TEST","CONTEST","POTA","SOTA",
            "SKCC","FISTS","NAQCC","CWOPS","ARRL","QRPARCI","MEMBER","NR","NUMBER","AGE","YRS","YEARS",
            "LIC","LICENSED","SINCE","RETIRED","WORKING","HOME","SHACK","STATION","FIELD","DAY",
            "IC","IC7300","IC7610","IC705","IC7851","FT991","FT991A","FTDX10","FTDX101","K3","K4",
            "KX2","KX3","TS590","TS890","ELECRAFT","ICOM","YAESU","KENWOOD","FLEX","HOMEBREW","HB",
            "WIN","RAIN","SNOW","SUN","SUNNY","CLOUDY","COLD","HOT","WARM","WINDY","CLEAR","FOG",
            "TONITE","TONIGHT","TODAY","NOW","TMRW","MORNING","EVENING","NIGHT","LATE","EARLY",
            // common plain-language words
            "A","I","THE","AND","IS","IT","IN","ON","OF","TO","FOR","WITH","BUT","NOT","GOT","GET",
            "HERE","THERE","THIS","THAT","HAVE","HAS","HAD","WAS","ARE","BE","BEEN","MY","ME","YOU",
            "YOUR","WE","OUR","NO","YES","SO","IF","OR","AS","BY","FROM","OUT","OVER","JUST","SOME",
            "MOST","MORE","LOT","MANY","MUCH","LITTLE","BIT","LAST","FIRST","NEXT","BACK","NEW","OLD",
            "BEGAN","BEGIN","STARTED","STABLE","FINE","WELL","GREAT","FUN","THANKS","THANK","CALL",
            "CALLING","LISTENING","QRU","STRONG","WEAK","LOUD","SIGNAL","SIGS","SIG","COPY","REPORT",
            "PEARL","SOLID","FADING","NOISE","NOISY","HIGH","LOW","BAD","BETTER","AGAIN","NEAR",
            "TWO","ONE","THREE","FOUR","FIVE","TEN","HUNDRED","WATER","ROAD","CITY","STATE",
        };
        // clang-format on
        for (const char *w : words)
            m_vocab.insert(w);
    }

    void reset()
    {
        m_tokens.clear();
    }
    bool pending() const
    {
        return !m_tokens.empty();
    }

    // Add a decoded word. softBefore = the break before it was short enough
    // that it might be inside a word.
    // internal = the break was a long letter gap INSIDE what timing called
    // one word; joining it back is the default unless the pieces score better.
    void push(const std::string &word, bool softBefore, std::string &out, bool internal = false)
    {
        if (word.empty())
            return;
        const bool soft = (softBefore || internal) && !m_tokens.empty();
        m_tokens.push_back({word, soft, internal && soft});
        // Keep latency bounded on long overs: settle all but the last few.
        if (m_tokens.size() >= 7)
            settle(static_cast<int>(m_tokens.size()) - 3, out);
    }

    // Hard break (long silence): settle everything.
    void flush(std::string &out)
    {
        settle(static_cast<int>(m_tokens.size()), out);
    }

private:
    struct Token
    {
        std::string text;
        bool softBefore;
        bool internal;
    };

    static bool isDigits(const std::string &s)
    {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    // Amateur callsign shape: optional prefix, at least one digit after
    // 1-3 prefix characters, 1-4 letter suffix (also handles /P, /QRP).
    static bool isCall(const std::string &s0)
    {
        std::string s = s0;
        const size_t slash = s.find('/');
        if (slash != std::string::npos)
            s = s.substr(0, slash);
        if (s.size() < 3 || s.size() > 7)
            return false;
        size_t i = 0;
        size_t pre = 0;
        while (i < s.size() && pre < 2 && std::isalnum(static_cast<unsigned char>(s[i])))
        {
            if (pre >= 1 && std::isdigit(static_cast<unsigned char>(s[i])))
                break;
            ++i;
            ++pre;
        }
        if (pre == 0 || i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
        ++i;
        size_t suf = 0;
        while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i])))
        {
            ++i;
            ++suf;
        }
        if (i != s.size() || suf < 1 || suf > 3)
            return false;
        // Must contain at least one letter before the digit (prefix like K, W5, 9A...).
        bool letter = false;
        for (size_t k = 0; k < pre; ++k)
            letter |= std::isalpha(static_cast<unsigned char>(s[k])) != 0;
        return letter || pre >= 2;
    }

    static bool isRst(const std::string &s)
    {
        if (s.size() != 3)
            return false;
        const bool r = s[0] >= '1' && s[0] <= '5';
        const bool st = (s[1] >= '1' && s[1] <= '9') || s[1] == 'N';
        const bool t = (s[2] >= '1' && s[2] <= '9') || s[2] == 'N';
        return r && st && t;
    }

    float score(const std::string &w, int nParts) const
    {
        // Never glue punctuation / prosigns ("= =" stays two breaks).
        if (nParts > 1)
        {
            for (char c : w)
                if (!std::isalnum(static_cast<unsigned char>(c)))
                    return -100.0f;
        }
        float sc;
        // Short "words" are weak evidence: R, K, I, ES... also show up as
        // fragments of longer words.
        if (m_vocab.count(w))
            sc = w.size() == 1 ? 0.5f : w.size() == 2 ? 1.5f : 3.0f;
        else if (isCall(w))
            sc = 3.0f;
        else if (isRst(w))
            sc = 2.5f;
        else if (isDigits(w) && w.size() == 1)
            sc = 0.0f;
        else if (isDigits(w) && w.size() <= 5)
            sc = 1.5f;
        else if (w.size() <= 2)
            sc = -1.0f; // stray fragment
        else
            sc = 0.0f; // unknown word
        // Joining is a claim; make it cost a little so ties stay split.
        return sc - 0.5f * static_cast<float>(nParts - 1);
    }

    // Settle the first 'count' tokens (best joining by DP) and output them.
    void settle(int count, std::string &out)
    {
        if (count <= 0)
            return;
        // DP over tokens [0, count): join runs of up to 5 tokens across soft breaks.
        std::vector<float> best(count + 1, -1e9f);
        std::vector<int> from(count + 1, -1);
        best[0] = 0.0f;
        for (int i = 1; i <= count; ++i)
        {
            std::string w;
            for (int L = 1; L <= 5 && L <= i; ++L)
            {
                const int s = i - L;
                // Run [s, i) may only cross soft breaks.
                if (L > 1 && !m_tokens[s + 1].softBefore)
                    break;
                w = m_tokens[s].text + w; // grows leftwards
                float c = best[s] + score(w, L);
                for (int k = s + 1; k < i; ++k)
                    if (m_tokens[k].internal)
                        c += 0.6f;
                if (c > best[i])
                {
                    best[i] = c;
                    from[i] = s;
                }
            }
        }
        std::vector<std::string> words;
        for (int i = count; i > 0; i = from[i])
        {
            std::string w;
            for (int k = from[i]; k < i; ++k)
                w += m_tokens[k].text;
            words.push_back(w);
        }
        std::reverse(words.begin(), words.end());
        for (const std::string &w : words)
        {
            out += w;
            out.push_back(' ');
        }
        m_tokens.erase(m_tokens.begin(), m_tokens.begin() + count);
        if (!m_tokens.empty())
        {
            m_tokens.front().softBefore = false;
            m_tokens.front().internal = false;
        }
    }

    std::vector<Token> m_tokens;
    std::unordered_set<std::string> m_vocab;
};
