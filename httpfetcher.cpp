#include "httpfetcher.h"

#include <QAuthenticator>
#include <QDateTime>
#include <QIODevice>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>

namespace {

constexpr int TransferTimeoutMs = 60'000;
constexpr int MaxRedirects = 10;

bool isHttp(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// An HTTP date: "Sun, 06 Nov 1994 08:49:37 GMT" (RFC 2822 parsing, which knows no "GMT", covers it
// as "+0000"), or the obsolete RFC 850 ("Sunday, 06-Nov-94 08:49:37 GMT") and asctime
// ("Sun Nov  6 08:49:37 1994") forms. Always UTC.
QDateTime httpDate(const QByteArray &value)
{
    const QString text = QString::fromLatin1(value).simplified();
    QString rfc2822 = text;
    if (rfc2822.endsWith(QLatin1String(" GMT")))
        rfc2822 = rfc2822.chopped(3) + QLatin1String("+0000");
    QDateTime date = QDateTime::fromString(rfc2822, Qt::RFC2822Date);
    if (date.isValid())
        return date;
    const QLocale c = QLocale::c();
    QString rest = text.mid(text.indexOf(u',') + 1).trimmed();
    rest.remove(QLatin1String(" GMT"));
    date = c.toDateTime(rest, QStringLiteral("dd-MMM-yy HH:mm:ss"));
    if (date.isValid()) {
        if (date.date().year() < 1970)
            date = date.addYears(100);
    } else {
        date = c.toDateTime(text, QStringLiteral("ddd MMM d HH:mm:ss yyyy"));
    }
    if (date.isValid())
        date.setTimeZone(QTimeZone::UTC);
    return date;
}

} // namespace

qint64 HttpFetcher::retryAfterMs(const QByteArray &value, const QByteArray &serverDate)
{
    const QByteArray trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return -1;
    bool isNumber = false;
    const qint64 seconds = trimmed.toLongLong(&isNumber);
    if (isNumber)
        return seconds < 0 ? -1 : qMin(seconds, qint64(365) * 24 * 3600) * 1000;
    const QDateTime when = httpDate(trimmed);
    if (!when.isValid())
        return -1;
    QDateTime now = httpDate(serverDate);
    if (!now.isValid())
        now = QDateTime::currentDateTimeUtc();
    return qMax(qint64(0), now.msecsTo(when));
}

HttpFetcher::HttpFetcher(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    // Asked only when both Basic and Bearer are filled in: then Bearer goes with the request and
    // Basic is the answer to the server's 401. Once per request: wrong credentials end it with 401.
    connect(m_manager, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const bool basic = !m_credentials.user.isEmpty() || !m_credentials.password.isEmpty();
                if (reply != m_reply || m_basicAnswered || !basic || m_credentials.bearerToken.isEmpty()
                    || !mayGetCredentials(reply->url()))
                    return;
                m_basicAnswered = true;
                authenticator->setUser(m_credentials.user);
                authenticator->setPassword(m_credentials.password);
            });
}

void HttpFetcher::setCredentials(const Credentials &credentials)
{
    m_credentials = credentials;
    m_manager->clearAccessCache();
}

void HttpFetcher::get(const QUrl &url, QIODevice *output, qint64 maxBytes)
{
    Q_ASSERT(!m_reply);
    m_output = output;
    m_maxBytes = maxBytes;
    m_written = 0;
    m_redirects = 0;
    m_aborted = false;
    m_failure.clear();
    m_basicAnswered = false;
    m_downgraded = false;
    m_result = Result{};
    send(url);
}

void HttpFetcher::abort()
{
    if (!m_reply)
        return;
    m_aborted = true;
    m_reply->abort(); // finished() follows
}

bool HttpFetcher::mayGetCredentials(const QUrl &url) const
{
    return !m_downgraded && !m_credentials.host.isEmpty()
           && url.host().compare(m_credentials.host, Qt::CaseInsensitive) == 0;
}

void HttpFetcher::send(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(TransferTimeoutMs);
    if (!m_userAgent.isEmpty())
        request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    if (mayGetCredentials(url)) {
        const bool basic = !m_credentials.user.isEmpty() || !m_credentials.password.isEmpty();
        if (!m_credentials.bearerToken.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + m_credentials.bearerToken.toUtf8());
        else if (basic)
            request.setRawHeader("Authorization",
                                 "Basic " + (m_credentials.user + ':' + m_credentials.password).toUtf8().toBase64());
    }

    m_result.finalUrl = url;
    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &HttpFetcher::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &HttpFetcher::progress);
    connect(m_reply, &QNetworkReply::finished, this, &HttpFetcher::onFinished);
}

void HttpFetcher::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray data = m_reply->readAll();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300 || !m_failure.isEmpty())
        return; // a redirect's or an error's body: not the file
    if (m_maxBytes > 0 && m_written + data.size() > m_maxBytes) {
        m_failure = tr("Відповідь завелика (понад %1 МБ).").arg(m_maxBytes / (1024 * 1024));
        m_reply->abort();
        return;
    }
    if (m_output) {
        if (m_output->write(data) != data.size()) {
            m_failure = tr("Не вдалося записати файл: %1").arg(m_output->errorString());
            m_reply->abort();
            return;
        }
    } else {
        m_result.body += data;
    }
    m_written += data.size();
}

void HttpFetcher::onFinished()
{
    QNetworkReply *reply = m_reply;
    if (!reply)
        return;
    onReadyRead(); // whatever is left
    m_reply = nullptr;
    reply->deleteLater();

    if (m_aborted) {
        finish(false, QString());
        return;
    }
    if (!m_failure.isEmpty()) {
        finish(false, m_failure);
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const QUrl location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (location.isEmpty()) {
            finish(false, tr("HTTP %1: перенаправлення без адреси.").arg(status));
            return;
        }
        if (++m_redirects > MaxRedirects) {
            finish(false, tr("Забагато перенаправлень."));
            return;
        }
        const QUrl next = reply->url().resolved(location);
        if (!isHttp(next)) {
            finish(false, tr("Перенаправлення на непідтримувану адресу: %1").arg(next.toString()));
            return;
        }
        if (reply->url().scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
            && next.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) == 0)
            m_downgraded = true; // no credentials over plain http from here on
        send(next);
        return;
    }

    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        QString error;
        if (status >= 300) {
            const QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
            error = QStringLiteral("HTTP %1 %2").arg(status).arg(reason).trimmed();
        } else if (reply->error() == QNetworkReply::OperationCanceledError
                   || reply->error() == QNetworkReply::TimeoutError) {
            error = tr("Сервер не відповідав %1 с.").arg(TransferTimeoutMs / 1000);
        } else if (reply->error() != QNetworkReply::NoError) {
            error = reply->errorString();
        } else {
            error = QStringLiteral("HTTP %1").arg(status);
        }
        if (status > 0)
            m_result.retryAfterMs = retryAfterMs(reply->rawHeader("Retry-After"), reply->rawHeader("Date"));
        finish(false, error);
        return;
    }

    m_result.contentType = reply->rawHeader("Content-Type");
    finish(true, QString());
}

void HttpFetcher::finish(bool ok, const QString &error)
{
    m_result.ok = ok;
    m_result.aborted = m_aborted;
    m_result.error = error;
    emit finished(m_result); // a handler may start the next get() right away
}
