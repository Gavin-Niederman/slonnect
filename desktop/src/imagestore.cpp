#include "imagestore.h"

#include <QImage>
#include <QUrl>

static ImageStore *s_instance = nullptr;

ImageStore::ImageStore()
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
{
    s_instance = this;
}

ImageStore *ImageStore::instance()
{
    return s_instance;
}

QString ImageStore::urlFor(const QString &key)
{
    return QStringLiteral("image://slonnect/") + QString::fromLatin1(QUrl::toPercentEncoding(key));
}

void ImageStore::insert(const QString &key, const QByteArray &encoded)
{
    QWriteLocker lock(&m_lock);
    m_images.insert(key, encoded);
}

bool ImageStore::contains(const QString &key) const
{
    QReadLocker lock(&m_lock);
    return m_images.contains(key);
}

QImage ImageStore::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QByteArray bytes;
    {
        QReadLocker lock(&m_lock);
        bytes = m_images.value(QUrl::fromPercentEncoding(id.toLatin1()));
    }
    QImage image = QImage::fromData(bytes);
    if (size)
        *size = image.size();
    if (!image.isNull() && requestedSize.isValid() && (requestedSize.width() < image.width() || requestedSize.height() < image.height()))
        image = image.scaled(requestedSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return image;
}
