#include "htmllinks.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace HtmlLinks {

namespace {

#ifdef Q_OS_WIN
// Qt without ICU decodes UTF-8/16/32 and Latin-1 only; windows-1251, koi8-u & co. go through
// Windows' own code pages. 0: not known.
UINT windowsCodePage(const QByteArray &charset)
{
    static const QHash<QByteArray, UINT> pages = {
        {"koi8-r", 20866}, {"koi8-u", 21866}, {"koi8-ru", 21866}, {"ibm866", 866}, {"cp866", 866},
        {"iso-8859-2", 28592}, {"iso-8859-3", 28593}, {"iso-8859-4", 28594}, {"iso-8859-5", 28595},
        {"iso-8859-6", 28596}, {"iso-8859-7", 28597}, {"iso-8859-8", 28598}, {"iso-8859-9", 28599},
        {"iso-8859-13", 28603}, {"iso-8859-15", 28605}, {"x-mac-cyrillic", 10007},
        {"gb2312", 936}, {"gbk", 936}, {"gb18030", 54936}, {"big5", 950}, {"shift_jis", 932},
        {"sjis", 932}, {"euc-jp", 20932}, {"euc-kr", 949}, {"ks_c_5601-1987", 949}, {"tis-620", 874},
    };
    const QByteArray name = charset.trimmed().toLower();
    if (const auto it = pages.constFind(name); it != pages.cend())
        return it.value();
    // windows-1251, cp1251, x-cp1251
    static const QRegularExpression numbered(QStringLiteral("^(?:x-)?(?:windows-|cp)([0-9]{3,5})$"));
    const QRegularExpressionMatch match = numbered.match(QString::fromLatin1(name));
    return match.hasMatch() ? match.captured(1).toUInt() : 0;
}

bool decodeWithWindows(const QByteArray &data, const QByteArray &charset, QString *text)
{
    const UINT page = windowsCodePage(charset);
    if (page == 0 || !IsValidCodePage(page))
        return false;
    if (data.isEmpty()) {
        text->clear();
        return true;
    }
    const int size = MultiByteToWideChar(page, 0, data.constData(), int(data.size()), nullptr, 0);
    if (size <= 0)
        return false;
    text->resize(size);
    MultiByteToWideChar(page, 0, data.constData(), int(data.size()), reinterpret_cast<wchar_t *>(text->data()), size);
    return true;
}
#endif

constexpr auto Unicode = QRegularExpression::UseUnicodePropertiesOption;

// HTML's own whitespace (not every Unicode space: a no-break space is part of a value).
bool isHtmlSpace(QChar c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

QStringList splitHtmlSpaces(const QString &text)
{
    static const QRegularExpression spaces(QStringLiteral("[ \\t\\n\\r\\f]+"), Unicode);
    return text.split(spaces, Qt::SkipEmptyParts);
}

struct Element
{
    QString name; // lower case
    QList<QPair<QString, QString>> attributes; // names lower case, values decoded, first one wins
    qsizetype bodyStart = -1; // -1: no body (void element, "/>", or never closed)
    qsizetype bodyEnd = -1;

    const QString *attribute(const QString &lowerName) const
    {
        for (const auto &attribute : attributes)
            if (attribute.first == lowerName)
                return &attribute.second;
        return nullptr;
    }
};

bool isVoidElement(const QString &name)
{
    static const QSet<QString> names = {"area", "base", "br", "col", "embed", "hr", "img", "input",
                                        "link", "meta", "param", "source", "track", "wbr"};
    return names.contains(name);
}

// Their content is text, not markup: it is taken as it is up to the matching end tag.
bool isRawTextElement(const QString &name)
{
    static const QSet<QString> names = {"script", "style", "textarea", "title", "xmp"};
    return names.contains(name);
}

QList<Element> parseElements(const QString &html)
{
    static const QRegularExpression startTag(QStringLiteral("<([A-Za-z][^ \\t\\n\\r\\f/>]*)"), Unicode);
    static const QRegularExpression endTag(QStringLiteral("</([A-Za-z][^ \\t\\n\\r\\f/>]*)[^>]*>"), Unicode);
    static const QRegularExpression attribute(
        QStringLiteral("([^ \\t\\n\\r\\f\"'>/=]+)(?:[ \\t\\n\\r\\f]*=[ \\t\\n\\r\\f]*"
                       "(?:\"([^\"]*)\"|'([^']*)'|([^ \\t\\n\\r\\f>]+)))?"),
        Unicode);
    const auto anchored = [](const QRegularExpression &re, const QString &text, qsizetype at) {
        return re.match(text, at, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    };

    QList<Element> elements;
    QList<qsizetype> open; // indexes of elements waiting for their end tag
    const qsizetype size = html.size();
    qsizetype pos = 0;
    while (pos < size) {
        pos = html.indexOf('<', pos);
        if (pos < 0)
            break;

        if (html.mid(pos, 4) == QLatin1String("<!--")) {
            const qsizetype end = html.indexOf(QLatin1String("-->"), pos + 4);
            pos = end < 0 ? size : end + 3;
            continue;
        }
        if (pos + 1 < size && (html[pos + 1] == '!' || html[pos + 1] == '?')) {
            const qsizetype end = html.indexOf('>', pos);
            pos = end < 0 ? size : end + 1;
            continue;
        }

        if (pos + 1 < size && html[pos + 1] == '/') {
            const QRegularExpressionMatch match = anchored(endTag, html, pos);
            if (!match.hasMatch()) {
                pos += 2;
                continue;
            }
            const QString name = match.captured(1).toLower();
            // The nearest open element of that name; the ones opened after it end here too.
            for (qsizetype i = open.size() - 1; i >= 0; --i) {
                if (elements[open[i]].name != name)
                    continue;
                for (qsizetype j = open.size() - 1; j >= i; --j)
                    elements[open[j]].bodyEnd = pos;
                open.resize(i);
                break;
            }
            pos = match.capturedEnd();
            continue;
        }

        const QRegularExpressionMatch name = anchored(startTag, html, pos);
        if (!name.hasMatch()) {
            ++pos; // a lone '<' in the text
            continue;
        }
        Element element;
        element.name = name.captured(1).toLower();
        qsizetype p = name.capturedEnd();
        bool selfClosing = false;
        bool closed = false;
        while (p < size) {
            const QChar c = html[p];
            if (isHtmlSpace(c)) {
                ++p;
            } else if (c == '>') {
                ++p;
                closed = true;
                break;
            } else if (c == '/') {
                ++p;
                if (p < size && html[p] == '>') {
                    ++p;
                    selfClosing = closed = true;
                    break;
                }
            } else {
                const QRegularExpressionMatch match = anchored(attribute, html, p);
                if (!match.hasMatch()) {
                    ++p; // a stray '=' or quote
                    continue;
                }
                const QString attributeName = match.captured(1).toLower();
                QString value;
                for (int group = 2; group <= 4; ++group)
                    if (match.capturedStart(group) >= 0)
                        value = match.captured(group);
                if (!element.attribute(attributeName))
                    element.attributes.append({attributeName, decodeEntities(value)});
                p = match.capturedEnd();
            }
        }
        if (!closed) // the page ends inside the tag
            break;
        pos = p;

        if (isRawTextElement(element.name) && !selfClosing) {
            const QRegularExpression rawEnd(QStringLiteral("</%1[ \\t\\n\\r\\f/>]").arg(QRegularExpression::escape(element.name)),
                                            QRegularExpression::CaseInsensitiveOption | Unicode);
            const QRegularExpressionMatch end = rawEnd.match(html, pos);
            element.bodyStart = pos;
            element.bodyEnd = end.hasMatch() ? end.capturedStart() : size;
            elements.append(element);
            if (!end.hasMatch())
                break;
            const qsizetype close = html.indexOf('>', end.capturedStart());
            pos = close < 0 ? size : close + 1;
            continue;
        }

        if (!isVoidElement(element.name) && !selfClosing) {
            element.bodyStart = pos;
            open.append(elements.size());
        }
        elements.append(element);
    }
    // Never closed: no body (where it would end is anyone's guess).
    for (const qsizetype index : std::as_const(open))
        elements[index].bodyStart = -1;
    return elements;
}

QString bodyText(const QString &html, const Element &element)
{
    static const QRegularExpression comments(QStringLiteral("<!--.*?(?:-->|$)"),
                                             QRegularExpression::DotMatchesEverythingOption | Unicode);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"), Unicode);
    QString text = html.mid(element.bodyStart, element.bodyEnd - element.bodyStart);
    if (!isRawTextElement(element.name)) {
        text.remove(comments);
        text.remove(tags);
    }
    return decodeEntities(text).trimmed();
}

bool matches(const Element &element, const DownloadProfile &profile, const QStringList &classes)
{
    if ((profile.tag == DownloadProfile::Tag::Img && element.name != QLatin1String("img"))
        || (profile.tag == DownloadProfile::Tag::A && element.name != QLatin1String("a")))
        return false;

    if (!classes.isEmpty()) {
        const QString *value = element.attribute(QStringLiteral("class"));
        if (!value)
            return false;
        const QStringList own = splitHtmlSpaces(*value);
        for (const QString &name : classes)
            if (!own.contains(name)) // class names are case-sensitive
                return false;
    }

    for (const auto &wanted : profile.attributes) {
        const QString *value = element.attribute(wanted.first.toLower());
        if (!value || QString::compare(*value, wanted.second, Qt::CaseInsensitive) != 0)
            return false;
    }
    return true;
}

bool startsWithHttp(const QString &text)
{
    return text.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
           || text.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
}

} // namespace

QString decodeEntities(const QString &text)
{
    if (!text.contains('&'))
        return text;
    static const QHash<QString, QString> named = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", QString(QChar(0xA0))},
    };
    static const QRegularExpression reference(QStringLiteral("&(?:#([0-9]{1,7})|#[xX]([0-9A-Fa-f]{1,6})|([A-Za-z]+));"),
                                              Unicode);
    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = reference.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QString replacement;
        if (match.capturedStart(1) >= 0 || match.capturedStart(2) >= 0) {
            const char32_t code = match.capturedStart(1) >= 0 ? match.captured(1).toUInt()
                                                              : match.captured(2).toUInt(nullptr, 16);
            if (code > 0 && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF))
                replacement = QString::fromUcs4(&code, 1);
        } else {
            replacement = named.value(match.captured(3)); // case-sensitive, as in HTML
        }
        if (replacement.isEmpty())
            continue; // unknown: stays as written
        result += QStringView(text).mid(last, match.capturedStart() - last);
        result += replacement;
        last = match.capturedEnd();
    }
    result += QStringView(text).mid(last);
    return result;
}

namespace {

QStringList stringsOf(const QString &html, const QList<Element> &elements, const DownloadProfile &profile)
{
    const QStringList classes = splitHtmlSpaces(profile.classes);
    const QString source = profile.source.trimmed().toLower();
    QStringList strings;
    for (const Element &element : elements) {
        if (!matches(element, profile, classes))
            continue;
        if (source.isEmpty()) {
            if (element.bodyStart >= 0)
                strings << bodyText(html, element);
        } else if (const QString *value = element.attribute(source)) {
            strings << value->trimmed();
        }
    }
    return strings;
}

} // namespace

QStringList extractStrings(const QString &html, const DownloadProfile &profile)
{
    return stringsOf(html, parseElements(html), profile);
}

QList<QUrl> extractLinks(const QString &html, const DownloadProfile &profile, const QUrl &pageUrl)
{
    const QList<Element> elements = parseElements(html);
    QUrl base = pageUrl;
    for (const Element &element : elements) {
        if (element.name != QLatin1String("base"))
            continue;
        if (const QString *href = element.attribute(QStringLiteral("href")); href && !href->trimmed().isEmpty()) {
            base = pageUrl.resolved(QUrl(href->trimmed()));
            break;
        }
    }

    const bool fromBody = profile.source.trimmed().isEmpty();
    QList<QUrl> links;
    QSet<QString> seen;
    for (const QString &text : stringsOf(html, elements, profile)) {
        QUrl url;
        if (startsWithHttp(text))
            url = QUrl(text);
        else if (!fromBody && !text.isEmpty() && !text.startsWith('#')) // a body's text is no address
            url = base.resolved(QUrl(text));
        if (!url.isValid() || url.host().isEmpty() || !startsWithHttp(url.toString()))
            continue;
        url.setFragment(QString()); // never sent to the server
        const QString key = url.toString(QUrl::FullyEncoded);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        links << url;
    }
    return links;
}

QString mimeTypeOf(const QByteArray &contentType)
{
    return QString::fromLatin1(contentType).section(';', 0, 0).trimmed().toLower();
}

QString decodePage(const QByteArray &data, const QByteArray &contentType)
{
    static const QRegularExpression headerCharset(QStringLiteral("charset\\s*=\\s*[\"']?([^\"'\\s;]+)"),
                                                  QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression metaCharset(
        QStringLiteral("<meta[^>]+charset\\s*=\\s*[\"']?([A-Za-z0-9_.:\\-]+)"),
        QRegularExpression::CaseInsensitiveOption);

    QByteArray charset;
    const QRegularExpressionMatch header = headerCharset.match(QString::fromLatin1(contentType));
    if (header.hasMatch())
        charset = header.captured(1).toLatin1();
    if (charset.isEmpty()) {
        if (const auto bom = QStringConverter::encodingForData(data)) {
            QStringDecoder decoder(*bom);
            return decoder.decode(data);
        }
        const QRegularExpressionMatch meta = metaCharset.match(QString::fromLatin1(data.left(8192)));
        if (meta.hasMatch())
            charset = meta.captured(1).toLatin1();
    }
    if (charset.isEmpty())
        charset = "UTF-8";

    QStringDecoder decoder(charset.constData());
    if (decoder.isValid())
        return decoder.decode(data);
#ifdef Q_OS_WIN
    QString text;
    if (decodeWithWindows(data, charset, &text))
        return text;
#endif
    return QString::fromLatin1(data);
}

} // namespace HtmlLinks
