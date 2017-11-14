#include "mpvwidget.h"
#include <stdexcept>
#include <QtGui/QOpenGLContext>
#include <QtCore/QMetaObject>
#include <QFontMetrics>
#include <QPainterPath>

static void wakeup(void *ctx)
{
    QMetaObject::invokeMethod((MpvWidget*)ctx, "on_mpv_events", Qt::QueuedConnection);
}

static void *get_proc_address(void *ctx, const char *name) {
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return NULL;
    return (void *)glctx->getProcAddress(QByteArray(name));
}

MpvWidget::MpvWidget(QWidget *parent, Qt::WindowFlags f)
    : QOpenGLWidget(parent, f)
{
    mpv = mpv::qt::Handle::FromRawHandle(mpv_create());
    if (!mpv)
        throw std::runtime_error("could not create mpv context");

    mpv_set_option_string(mpv, "terminal", "yes");
    mpv_set_option_string(mpv, "msg-level", "all=v");
    if (mpv_initialize(mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");

    // Make use of the MPV_SUB_API_OPENGL_CB API.
    mpv::qt::set_option_variant(mpv, "vo", "opengl-cb");

    // Request hw decoding, just for testing.
    mpv::qt::set_option_variant(mpv, "hwdec", "auto");

    mpv_gl = (mpv_opengl_cb_context *)mpv_get_sub_api(mpv, MPV_SUB_API_OPENGL_CB);
    if (!mpv_gl)
        throw std::runtime_error("OpenGL not compiled in");
    mpv_opengl_cb_set_update_callback(mpv_gl, MpvWidget::on_update, (void *)this);
    connect(this, SIGNAL(frameSwapped()), SLOT(swapped()));

    mpv_observe_property(mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(mpv, wakeup, this);

}



MpvWidget::~MpvWidget()
{
    makeCurrent();
    if (mpv_gl)
        mpv_opengl_cb_set_update_callback(mpv_gl, NULL, NULL);
    // Until this call is done, we need to make sure the player remains
    // alive. This is done implicitly with the mpv::qt::Handle instance
    // in this class.
    mpv_opengl_cb_uninit_gl(mpv_gl);
}

void MpvWidget::command(const QVariant& params)
{
    mpv::qt::command_variant(mpv, params);
}

void MpvWidget::setProperty(const QString& name, const QVariant& value)
{
    mpv::qt::set_property_variant(mpv, name, value);
}

QVariant MpvWidget::getProperty(const QString &name) const
{
    return mpv::qt::get_property_variant(mpv, name);
}


void MpvWidget::initializeGL()
{
    int r = mpv_opengl_cb_init_gl(mpv_gl, NULL, get_proc_address, NULL);
    if (r < 0)
        throw std::runtime_error("could not initialize OpenGL");
}

void MpvWidget::paintGL()
{
    mpv_opengl_cb_draw(mpv_gl, defaultFramebufferObject(), width(), -height());
}

void MpvWidget::swapped()
{
    mpv_opengl_cb_report_flip(mpv_gl, 0);
}

void MpvWidget::on_mpv_events()
{
    // Process all events, until the event queue is empty.
    while (mpv) {
        mpv_event *event = mpv_wait_event(mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handle_mpv_event(event);
    }
}

void MpvWidget::handle_mpv_event(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property *prop = (mpv_event_property *)event->data;
        if (strcmp(prop->name, "time-pos") == 0) {
            if (prop->format == MPV_FORMAT_DOUBLE) {
                double time = *(double *)prop->data;
                Q_EMIT positionChanged(time);
            }
        } else if (strcmp(prop->name, "duration") == 0) {
            if (prop->format == MPV_FORMAT_DOUBLE) {
                double time = *(double *)prop->data;
                Q_EMIT durationChanged(time);
            }
        }
        break;
    }
    default: ;
        // Ignore uninteresting or unknown events.
    }
}

// Make Qt invoke mpv_opengl_cb_draw() to draw a new/updated video frame.
void MpvWidget::maybeUpdate()
{
    // If the Qt window is not visible, Qt's update() will just skip rendering.
    // This confuses mpv's opengl-cb API, and may lead to small occasional
    // freezes due to video rendering timing out.
    // Handle this by manually redrawing.
    // Note: Qt doesn't seem to provide a way to query whether update() will
    //       be skipped, and the following code still fails when e.g. switching
    //       to a different workspace with a reparenting window manager.
    if (window()->isMinimized()) {
        makeCurrent();
        paintGL();
        context()->swapBuffers(context()->surface());
        swapped();
        doneCurrent();
    } else {
        update();
    }
}

void MpvWidget::on_update(void *ctx)
{
    QMetaObject::invokeMethod((MpvWidget*)ctx, "maybeUpdate");
}

/*************************DanmakuPlayer BEGIN*************************/


DanmakuPlayer::DanmakuPlayer(QWidget *parent, Qt::WindowFlags f) : MpvWidget(parent, f)

{
    setFocusPolicy(Qt::StrongFocus);
    initDanmaku();
    initDensityTimer();
}

DanmakuPlayer::~DanmakuPlayer()
{
    danmakuDensityTimer->deleteLater();
}

void DanmakuPlayer::addNewDanmaku(QString danmaku)
{
    danmakuPool[writeDanmakuIndex++] = "0" + danmaku; //0为未读
    writeDanmakuIndex = writeDanmakuIndex % 20;
}

void DanmakuPlayer::initDanmaku()
{
    writeDanmakuIndex = 0;
    readDanmakuIndex = 0;
    danmakuPool.clear();
    int i;
    for(i = 0; i < 20; i++)
    {
        danmakuPool << "NULL";
    }
}

void DanmakuPlayer::initDensityTimer()
{
    danmakuDensityTimer = new QTimer(this);
    connect(danmakuDensityTimer, &QTimer::timeout, this, &DanmakuPlayer::launchDanmaku);
    danmakuDensityTimer->start(100);
}

void DanmakuPlayer::launchDanmaku()
{
    if((danmakuPool[readDanmakuIndex] != "NULL") && (danmakuPool[readDanmakuIndex].at(0) != "1"))
    {
        int danmakuPos = getAvailDanmakuChannel() * (this->height() / 16);
        int danmakuSpeed = this->width() * 11;

        QLabel* danmaku;
        danmaku = new QLabel(this);

        danmakuPool[readDanmakuIndex].remove(0, 1);
        danmaku->setText(danmakuPool[readDanmakuIndex]);
        danmakuPool[readDanmakuIndex++].insert(0, "1");

        readDanmakuIndex = readDanmakuIndex % 20;
        danmaku->setStyleSheet("color: white; font-size: 18px; font-weight: bold");

        QGraphicsDropShadowEffect *danmakuTextShadowEffect = new QGraphicsDropShadowEffect(this);
        danmakuTextShadowEffect->setColor(QColor("#000000"));
        danmakuTextShadowEffect->setBlurRadius(4);
        danmakuTextShadowEffect->setOffset(1,1);
        danmaku->setGraphicsEffect(danmakuTextShadowEffect);

        QPropertyAnimation* mAnimation=new QPropertyAnimation(danmaku, "pos");
        mAnimation->setStartValue(QPoint(this->width(), danmakuPos));
        mAnimation->setEndValue(QPoint(-500, danmakuPos));
        mAnimation->setDuration(danmakuSpeed);
        mAnimation->setEasingCurve(QEasingCurve::Linear);
        danmaku->show();
        mAnimation->start();

        connect(this, &DanmakuPlayer::closeDanmaku, danmaku, &QLabel::close);
        connect(mAnimation, &QPropertyAnimation::finished, danmaku, &QLabel::deleteLater);
    }
    else
    {
        readDanmakuIndex++;
        readDanmakuIndex = readDanmakuIndex % 20;
        return;
    }
}


int DanmakuPlayer::getAvailDanmakuChannel()
{
    int flag = 0;
    int count = 16;
    int channel = 0;
    while(flag == 0 && count != 0)
    {
        channel = qrand() % 16;
        if(((quint32)pow(2, channel) & danmakuChannelMask) != 0)
        {
            danmakuChannelMask = danmakuChannelMask & ~(quint32)pow(2, channel);
            flag = 1;
        }
        count--;
    }
    if(count == 0)
    {
        danmakuChannelMask = 0x0000FFFF;
        return getAvailDanmakuChannel();
    }
    else
    {
        return channel;
    }
}

void DanmakuPlayer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_D:
        danmakuShowFlag = !danmakuShowFlag;
        if(danmakuShowFlag == false) {
            danmakuDensityTimer->stop();
            Q_EMIT closeDanmaku();
        }else {
            initDanmaku();
            danmakuDensityTimer->start(100);
        }
        break;
    default:
        break;
    }
    MpvWidget::keyPressEvent(event);
}
