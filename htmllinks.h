#ifndef HTMLLINKS_H
#define HTMLLINKS_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "downloadprofile.h"

// Finds the links a download profile asks for in an HTML page. A small tag tokenizer on regular
// expressions (no browser engine): comments, <!...>, <?...> and the content of script/style are
// skipped; a tag's body ends at its end tag (unclosed tags inside are closed with it).
namespace HtmlLinks {

// Character references: &amp; &lt; &gt; &quot; &apos; &nbsp; &#123; &#x7B; (others stay as written).
QString decodeEntities(const QString &text);

// The strings the profile picks, in page order: the source attribute's value, or the body's text
// (inner tags removed); entities decoded, trimmed. Tags without the source are skipped.
QStringList extractStrings(const QString &html, const DownloadProfile &profile);

// The links to download: extractStrings resolved against the page (its <base href> honoured) when
// they come from an attribute (a body's text is taken only if it is an absolute link), only
// http:// and https://, without the #fragment, each link once.
QList<QUrl> extractLinks(const QString &html, const DownloadProfile &profile, const QUrl &pageUrl);

// The page as text: charset of the Content-Type header, else a BOM, else <meta charset> /
// <meta http-equiv content="...charset=...">, else UTF-8. Charsets Qt can't decode (it has no ICU
// here: windows-1251, koi8-u...) go through Windows' code pages; unknown ones are read as Latin-1
// (links are ASCII mostly, so they survive).
QString decodePage(const QByteArray &data, const QByteArray &contentType);

// "text/html; charset=utf-8" -> "text/html".
QString mimeTypeOf(const QByteArray &contentType);

} // namespace HtmlLinks

#endif // HTMLLINKS_H
