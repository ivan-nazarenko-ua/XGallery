#ifndef HTTPFETCHER_H
#define HTTPFETCHER_H

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QIODevice;
class QNetworkAccessManager;

// One GET request at a time (the download manager sends nothing else). Redirects are followed
// here (at most 10), so the credentials go only where they belong: to the host of the page the
// user gave, and never from https down to http. Basic and/or Bearer, as filled in:
// - only Basic, or only Bearer: sent with the request;
// - both: Bearer with the request, Basic as the answer when the server asks for it (401).
// No data for 60 s ends the request with an error.
class HttpFetcher : public QObject
{
    Q_OBJECT

public:
    struct Credentials
    {
        QString user;
        QString password;
        QString bearerToken;
        QString host; // the only host they are sent to
    };

    struct Result
    {
        bool ok = false;
        bool aborted = false; // abort() was called
        QString error;        // when !ok and !aborted
        QUrl finalUrl;        // after redirects
        QByteArray contentType;
        QByteArray body;      // when no output device was given
    };

    explicit HttpFetcher(QObject *parent = nullptr);

    void setUserAgent(const QString &userAgent) { m_userAgent = userAgent; }
    // Also forgets the Basic credentials Qt cached from a 401 answer, so old ones never go out again.
    void setCredentials(const Credentials &credentials);

    // Starts a GET. The body (of a 2xx answer only) goes to output, or into Result::body when
    // output is null; maxBytes > 0 limits the size (larger: an error). finished() follows.
    void get(const QUrl &url, QIODevice *output, qint64 maxBytes = 0);
    void abort();
    bool isRunning() const { return m_reply; }

signals:
    void progress(qint64 received, qint64 total); // total -1: unknown
    void finished(const HttpFetcher::Result &result);

private:
    void send(const QUrl &url);
    void onReadyRead();
    void onFinished();
    void finish(bool ok, const QString &error);
    bool mayGetCredentials(const QUrl &url) const;

    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QString m_userAgent;
    Credentials m_credentials;
    QIODevice *m_output = nullptr;
    qint64 m_maxBytes = 0;
    qint64 m_written = 0;
    int m_redirects = 0;
    bool m_aborted = false;
    QString m_failure; // why the request was aborted here (too large, can't write)
    bool m_basicAnswered = false; // the server's 401 got the Basic credentials once already
    bool m_downgraded = false;    // a redirect went from https to http
    Result m_result;
};

#endif // HTTPFETCHER_H
