#pragma once

#include <QHash>
#include <QQuickImageProvider>
#include <QReadWriteLock>

// Encoded image bytes (attachment thumbnails, contact photos) keyed by id and
// served to QML as image://slonnect/<key>. Decoding happens on QML's image
// loader threads, never on the GUI thread.
class ImageStore : public QQuickImageProvider
{
public:
    ImageStore();

    static ImageStore *instance();
    static QString urlFor(const QString &key);

    void insert(const QString &key, const QByteArray &encoded);
    bool contains(const QString &key) const;

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    mutable QReadWriteLock m_lock;
    QHash<QString, QByteArray> m_images;
};
