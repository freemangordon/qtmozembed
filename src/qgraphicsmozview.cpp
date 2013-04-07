/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* vim: set ts=2 sw=2 et tw=79: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#define LOG_COMPONENT "QGraphicsMozView"

#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QTimer>
#include <QtOpenGL/QGLContext>
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#include <QInputContext>
#include <qjson/serializer.h>
#include <qjson/parser.h>
#else
#include <QJsonDocument>
#include <QJsonParseError>
#endif
#include "EmbedQtKeyUtils.h"

#include "qgraphicsmozview.h"
#include "qmozcontext.h"
#include "InputData.h"
#include "mozilla/embedlite/EmbedLog.h"
#include "mozilla/embedlite/EmbedLiteApp.h"

#include "qgraphicsmozview_p.h"

using namespace mozilla;
using namespace mozilla::embedlite;


QGraphicsMozView::QGraphicsMozView(QGraphicsItem* parent)
    : QGraphicsWidget(parent)
    , d(new QGraphicsMozViewPrivate(this))
    , mParentID(0)
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    setAcceptDrops(true);
    setAcceptTouchEvents(true);
    setFocusPolicy(Qt::StrongFocus);
    setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);

    setFlag(QGraphicsItem::ItemAcceptsInputMethod, true);

    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setFlag(QGraphicsItem::ItemIsFocusScope, true);
    setFlag(QGraphicsItem::ItemIsFocusable, true);
    setInputMethodHints(Qt::ImhPreferLowercase);

    d->mContext = QMozContext::GetInstance();
    if (!d->mContext->initialized()) {
        connect(d->mContext, SIGNAL(onInitialized()), this, SLOT(onInitialized()));
    } else {
        QTimer::singleShot(0, this, SLOT(onInitialized()));
    }
}

void
QGraphicsMozView::setParentID(unsigned aParentID)
{
    LOGT("mParentID:%u", aParentID);
    mParentID = aParentID;
    if (mParentID) {
        onInitialized();
    }
}

QGraphicsMozView::~QGraphicsMozView()
{
    d->mContext->GetApp()->DestroyView(d->mView);
    if (d->mView) {
        d->mView->SetListener(NULL);
    }
    delete d;
}

void
QGraphicsMozView::onInitialized()
{
    LOGT("mParentID:%u", mParentID);
    if (!d->mView) {
        d->mView = d->mContext->GetApp()->CreateView(mParentID);
        d->mView->SetListener(d);
    }
}

quint32
QGraphicsMozView::uniqueID() const
{
    return d->mView ? d->mView->GetUniqueID() : 0;
}

#if defined(GL_PROVIDER_EGL) && !defined(EGL_FORCE_SCISSOR_CLIP)
GLuint
QGraphicsMozView::loadShader(const char* src, GLenum type)
{
    GLuint shader;
    GLint compiled;

    // Create the shader object
    shader = glCreateShader(type);
    if (!shader)
      return 0;

    // Load the shader source
    glShaderSource(shader, 1, &src, NULL);

    // Compile the shader
    glCompileShader(shader);

    // Check the compile status
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      GLint infoLen = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
      if (infoLen > 1) {
        char* infoLog = (char*)malloc(sizeof(char) * infoLen);
        glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
        LOGT("Unable to compile the shader - %s", infoLog);
        free(infoLog);
      }
      glDeleteShader(shader);
      return 0;
    }

    return shader;
}

#define STENCIL_ARRAY_INDEX 0
void
QGraphicsMozView::createStencilClipProgram()
{
  GLuint fragmentShader;
  GLuint vertexShader;
  GLint linked;

  QString srcFragShader = "\
          uniform lowp vec3 f_color;\
          void main (void)\
          {\
            gl_FragColor = vec4(f_color[0], f_color[1], f_color[2], 0.0);\
          }";

  QString srcVertShader = "\
          attribute highp vec4 stencilVertex;\
          void main(void)\
          {\
            gl_Position = stencilVertex;\
          }";

  // Create the program object
  stencilProgramObject = glCreateProgram();
  if (!stencilProgramObject)
    return;

  // Load the shaders
  fragmentShader = loadShader(qPrintable(srcFragShader), GL_FRAGMENT_SHADER);
  vertexShader = loadShader(qPrintable(srcVertShader), GL_VERTEX_SHADER);

  if (!fragmentShader || !vertexShader)
    return;
  glAttachShader(stencilProgramObject, vertexShader);
  glAttachShader(stencilProgramObject, fragmentShader);

  // Bind stencilVertex to attribute STENCIL_ARRAY_INDEX
  glBindAttribLocation(stencilProgramObject,
                       STENCIL_ARRAY_INDEX,
                       "stencilVertex");

  // Link the program
  glLinkProgram(stencilProgramObject);

  // Check the link status
  glGetProgramiv(stencilProgramObject, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint infoLen = 0;
    glGetProgramiv(stencilProgramObject, GL_INFO_LOG_LENGTH, &infoLen);
    if (infoLen > 1) {
      char* infoLog = (char*)malloc(sizeof(char) * infoLen);
      glGetProgramInfoLog(stencilProgramObject, infoLen, NULL, infoLog);
      LOGT("Link failed - %s",infoLog);
      free(infoLog);
    }
    glDeleteProgram(stencilProgramObject);
    stencilProgramObject = 0;
  } else {
    colorUniform = glGetUniformLocation(stencilProgramObject, "f_color");
    if (colorUniform == -1) {
      LOGT("Could not bind uniform f_color");
    }
  }
}
#endif

/*
  This is a noop on non-EGL HW, clipping is done in gecko by using scissor test
  Using scissor test on EGL could be forced with defining EGL_FORCE_SCISSOR_CLIP
  */
void
QGraphicsMozView::StencilClipGLEnable(const QRect& r)
{
#if defined(GL_PROVIDER_EGL) && !defined(EGL_FORCE_SCISSOR_CLIP)
#define scaleGL(p, s) \
  (2.0f*(((GLfloat)p)/((GLfloat)s))-1.0f)

  GLfloat w = scene()->views()[0]->width();
  GLfloat h = scene()->views()[0]->height();
  QRectF rs = mapRectToScene(QRectF(r));

  GLfloat rsx = rs.x();
  GLfloat rsy = rs.y();
  GLfloat rsw = rs.width();
  GLfloat rsh = rs.height();

  GLfloat vertexs[] = {
    scaleGL(rsx, w),     scaleGL(h-(rsy+rsh), h), 0.0f,
    scaleGL(rsx+rsw, w), scaleGL(h-(rsy+rsh), h), 0.0f,
    scaleGL(rsx, w),     scaleGL(h-rsy, h),       0.0f,
    scaleGL(rsx+rsw, w), scaleGL(h-rsy, h),       0.0f
  };

  if (!stencilProgramObject) {
    createStencilClipProgram();
  }

  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);
  glEnable(GL_STENCIL_TEST);

  glStencilFunc(GL_NEVER, 1, 1);
  glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

  glUseProgram(stencilProgramObject);

  glUniform3f(colorUniform, 1.0f, 1.0f, 1.0f);
  glVertexAttribPointer(STENCIL_ARRAY_INDEX, 3, GL_FLOAT, GL_FALSE, 0, vertexs);
  glEnableVertexAttribArray(STENCIL_ARRAY_INDEX);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_EQUAL, 1, 1);

  glDisableVertexAttribArray(STENCIL_ARRAY_INDEX);
#endif
}

void
QGraphicsMozView::StencilClipGLDisable()
{
#if defined(GL_PROVIDER_EGL) && !defined(EGL_FORCE_SCISSOR_CLIP)
  glDisable(GL_STENCIL_TEST);
#endif
}

void
QGraphicsMozView::paint(QPainter* painter, const QStyleOptionGraphicsItem* opt, QWidget*)
{
    if (!d->mGraphicsViewAssigned) {
        d->mGraphicsViewAssigned = true;
        // Disable for future gl context in case if we did not get it yet
        if (d->mViewInitialized &&
            d->mContext->GetApp()->IsAccelerated() &&
            !QGLContext::currentContext()) {
            LOGT("Gecko is setup for GL rendering but no context available on paint, disable it");
            d->mContext->setIsAccelerated(false);
        }
        QGraphicsView* view = d->GetViewWidget();
        if (view) {
            connect(view, SIGNAL(displayEntered()), this, SLOT(onDisplayEntered()));
            connect(view, SIGNAL(displayExited()), this, SLOT(onDisplayExited()));
        }
    }

    QRect r = opt ? opt->exposedRect.toRect() : boundingRect().toRect();
    if (d->mViewInitialized) {
        QMatrix affine = painter->transform().toAffine();
        gfxMatrix matr(affine.m11(), affine.m12(), affine.m21(), affine.m22(), affine.dx(), affine.dy());
        bool changedState = d->mLastIsGoodRotation != matr.PreservesAxisAlignedRectangles();
        d->mLastIsGoodRotation = matr.PreservesAxisAlignedRectangles();
        if (d->mContext->GetApp()->IsAccelerated()) {
            d->mView->SetGLViewTransform(matr);
#if defined(EGL_FORCE_SCISSOR_CLIP)
            d->mView->SetViewClipping(0, 0, d->mSize.width(), d->mSize.height());
#endif
            if (changedState) {
                d->UpdateViewSize();
            }
            if (d->mLastIsGoodRotation) {
                // FIXME need to find proper rect using proper transform chain
                QRect paintRect = painter->transform().isRotating() ? affine.mapRect(r) : r;
                painter->beginNativePainting();
                StencilClipGLEnable(paintRect);

                d->mView->RenderGL();

                StencilClipGLDisable();
                painter->endNativePainting();
            }
        } else {
            if (d->mTempBufferImage.isNull() || d->mTempBufferImage.width() != r.width() || d->mTempBufferImage.height() != r.height()) {
                d->mTempBufferImage = QImage(r.size(), QImage::Format_RGB16);
            }
            {
                QPainter imgPainter(&d->mTempBufferImage);
                imgPainter.fillRect(r, d->mBgColor);
            }
            d->mView->RenderToImage(d->mTempBufferImage.bits(), d->mTempBufferImage.width(),
                                    d->mTempBufferImage.height(), d->mTempBufferImage.bytesPerLine(),
                                    d->mTempBufferImage.depth());
            painter->drawImage(QPoint(0, 0), d->mTempBufferImage);
        }
    } 
}

/*! \reimp
*/
QSizeF QGraphicsMozView::sizeHint(Qt::SizeHint which, const QSizeF& constraint) const
{
    if (which == Qt::PreferredSize)
        return QSizeF(800, 600); // ###
    return QGraphicsWidget::sizeHint(which, constraint);
}

/*! \reimp
*/
void QGraphicsMozView::setGeometry(const QRectF& rect)
{
    QGraphicsWidget::setGeometry(rect);

    // NOTE: call geometry() as setGeometry ensures that
    // the geometry is within legal bounds (minimumSize, maximumSize)
    d->mSize = geometry().size().toSize();
    d->UpdateViewSize();
}

QUrl QGraphicsMozView::url() const
{
    return QUrl(d->mLocation);
}

void QGraphicsMozView::setUrl(const QUrl& url)
{
    if (url.isEmpty())
        return;

    if (!d->mViewInitialized) {
        return;
    }
    LOGT("url: %s", url.toString().toUtf8().data());
    d->mView->LoadURL(url.toString().toUtf8().data());
}

void QGraphicsMozView::load(const QString& url)
{
    if (url.isEmpty())
        return;

    if (!d->mViewInitialized) {
        return;
    }
    LOGT("url: %s", url.toUtf8().data());
    d->mView->LoadURL(QUrl::fromUserInput(url).toString().toUtf8().data());
}

void QGraphicsMozView::loadFrameScript(const QString& name)
{
    LOGT("script:%s", name.toUtf8().data());
    d->mView->LoadFrameScript(name.toUtf8().data());
}

void QGraphicsMozView::addMessageListener(const QString& name)
{
    LOGT("name:%s", name.toUtf8().data());
    d->mView->AddMessageListener(name.toUtf8().data());
}

void QGraphicsMozView::sendAsyncMessage(const QString& name, const QVariant& variant)
{
    if (!d->mViewInitialized)
        return;

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
    QJson::Serializer serializer;
    QByteArray array = serializer.serialize(variant);
#else
    QJsonDocument doc = QJsonDocument::fromVariant(variant);
    QByteArray array = doc.toJson();
#endif

    d->mView->SendAsyncMessage((const PRUnichar*)name.constData(), NS_ConvertUTF8toUTF16(array.constData()).get());

}

QPointF QGraphicsMozView::scrollableOffset() const
{
    return d->mScrollableOffset;
}

float QGraphicsMozView::resolution() const
{
    return d->mContentResolution;
}

QRect QGraphicsMozView::contentRect() const
{
    return d->mContentRect;
}

QSize QGraphicsMozView::scrollableSize() const
{
    return d->mScrollableSize;
}

QString QGraphicsMozView::title() const
{
    return d->mTitle;
}

int QGraphicsMozView::loadProgress() const
{
    return d->mProgress;
}

bool QGraphicsMozView::canGoBack() const
{
    return d->mCanGoBack;
}

bool QGraphicsMozView::canGoForward() const
{
    return d->mCanGoForward;
}

bool QGraphicsMozView::loading() const
{
    return d->mIsLoading;
}

void QGraphicsMozView::loadHtml(const QString& html, const QUrl& baseUrl)
{
    LOGT();
}

void QGraphicsMozView::goBack()
{
    LOGT();
    if (!d->mViewInitialized)
        return;
    d->mView->GoBack();
}

void QGraphicsMozView::goForward()
{
    LOGT();
    if (!d->mViewInitialized)
        return;
    d->mView->GoForward();
}

void QGraphicsMozView::stop()
{
    LOGT();
    if (!d->mViewInitialized)
        return;
    d->mView->StopLoad();
}

void QGraphicsMozView::reload()
{
    LOGT();
    if (!d->mViewInitialized)
        return;
    d->mView->Reload(false);
}

bool QGraphicsMozView::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
        d->touchEvent(static_cast<QTouchEvent*>(event));
        return true;
    }
    case QEvent::Show: {
        LOGT("Event Show: curCtx:%p", QGLContext::currentContext());
        break;
    }
    case QEvent::Hide: {
        LOGT("Event Hide");
        break;
    }
    default:
        break;
    }

    // Here so that it can be reimplemented without breaking ABI.
    return QGraphicsWidget::event(event);
}

void QGraphicsMozView::onDisplayEntered()
{
    if (!d->mView) {
        return;
    }
    d->mView->SetIsActive(true);
    d->mView->ResumeTimeouts();
}

void QGraphicsMozView::onDisplayExited()
{
    if (!d->mView) {
        return;
    }
    d->mView->SetIsActive(false);
    d->mView->SuspendTimeouts();

}

void QGraphicsMozView::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (d->mViewInitialized && !d->mPendingTouchEvent) {
        const bool accepted = e->isAccepted();
        MultiTouchInput event(MultiTouchInput::MULTITOUCH_MOVE, d->mPanningTime.elapsed());
        event.mTouches.AppendElement(SingleTouchData(0,
                                     nsIntPoint(e->pos().x(), e->pos().y()),
                                     nsIntPoint(1, 1),
                                     180.0f,
                                     1.0f));
        d->ReceiveInputEvent(event);
        e->setAccepted(accepted);
    }

    if (!e->isAccepted())
        QGraphicsItem::mouseMoveEvent(e);
}

void QGraphicsMozView::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    d->mPanningTime.restart();
    forceActiveFocus();
    if (d->mViewInitialized && !d->mPendingTouchEvent) {
        const bool accepted = e->isAccepted();
        MultiTouchInput event(MultiTouchInput::MULTITOUCH_START, d->mPanningTime.elapsed());
        event.mTouches.AppendElement(SingleTouchData(0,
                                     nsIntPoint(e->pos().x(), e->pos().y()),
                                     nsIntPoint(1, 1),
                                     180.0f,
                                     1.0f));
        d->ReceiveInputEvent(event);
        e->setAccepted(accepted);
    }

    if (!e->isAccepted())
        QGraphicsItem::mouseMoveEvent(e);
}

void QGraphicsMozView::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (d->mViewInitialized && !d->mPendingTouchEvent) {
        const bool accepted = e->isAccepted();
        MultiTouchInput event(MultiTouchInput::MULTITOUCH_END, d->mPanningTime.elapsed());
        event.mTouches.AppendElement(SingleTouchData(0,
                                     nsIntPoint(e->pos().x(), e->pos().y()),
                                     nsIntPoint(1, 1),
                                     180.0f,
                                     1.0f));
        d->ReceiveInputEvent(event);
        e->setAccepted(accepted);
    }
    if (d->mPendingTouchEvent) {
        d->mPendingTouchEvent = false;
    }

    if (!e->isAccepted())
        QGraphicsItem::mouseMoveEvent(e);
}

void QGraphicsMozView::forceActiveFocus()
{
    QGraphicsItem *parent = parentItem();
    while (parent) {
        if (parent->flags() & QGraphicsItem::ItemIsFocusScope)
            parent->setFocus(Qt::OtherFocusReason);
        parent = parent->parentItem();
    }

    setFocus(Qt::OtherFocusReason);
    if (d->mViewInitialized) {
        d->mView->SetIsActive(true);
    }
}

void QGraphicsMozView::inputMethodEvent(QInputMethodEvent* event)
{
    LOGT("cStr:%s, preStr:%s, replLen:%i, replSt:%i", event->commitString().toUtf8().data(), event->preeditString().toUtf8().data(), event->replacementLength(), event->replacementStart());
    if (d->mViewInitialized) {
        d->mView->SendTextEvent(event->commitString().toUtf8().data(), event->preeditString().toUtf8().data());
    }
}

void QGraphicsMozView::keyPressEvent(QKeyEvent* event)
{
    if (!d->mViewInitialized)
        return;

    LOGT();
    int32_t gmodifiers = MozKey::QtModifierToDOMModifier(event->modifiers());
    int32_t domKeyCode = MozKey::QtKeyCodeToDOMKeyCode(event->key(), event->modifiers());
    int32_t charCode = 0;
    if (event->text().length() && event->text()[0].isPrint()) {
        charCode = (int32_t)event->text()[0].unicode();
        if (getenv("USE_TEXT_EVENTS")) {
            return;
        }
    }

#if !defined(Q_WS_MAEMO_5)
    d->mView->SendKeyPress(domKeyCode, gmodifiers, charCode);
#endif
}

void QGraphicsMozView::keyReleaseEvent(QKeyEvent* event)
{
    if (!d->mViewInitialized)
        return;

    LOGT();
    int32_t gmodifiers = MozKey::QtModifierToDOMModifier(event->modifiers());
    int32_t domKeyCode = MozKey::QtKeyCodeToDOMKeyCode(event->key(), event->modifiers());
    int32_t charCode = 0;
    if (event->text().length() && event->text()[0].isPrint()) {
        charCode = (int32_t)event->text()[0].unicode();
        if (getenv("USE_TEXT_EVENTS")) {
            d->mView->SendTextEvent(event->text().toUtf8().data(), "");
            return;
        }
    }
#if defined(Q_WS_MAEMO_5)
    d->mView->SendKeyPress(domKeyCode, gmodifiers, charCode);
#endif
    d->mView->SendKeyRelease(domKeyCode, gmodifiers, charCode);
}

QVariant
QGraphicsMozView::inputMethodQuery(Qt::InputMethodQuery aQuery) const
{
    static bool commitNow = getenv("DO_FAST_COMMIT") != 0;
    return commitNow ? QVariant(0) : QVariant();
}

void
QGraphicsMozView::newWindow(const QString& url)
{
    LOGT("New Window: %s", url.toUtf8().data());
}
