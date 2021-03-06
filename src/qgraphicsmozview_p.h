/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* vim: set ts=2 sw=2 et tw=79: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef qgraphicsmozview_p_h
#define qgraphicsmozview_p_h

#include <QColor>
#include <QImage>
#include <QSize>
#include <QTime>
#include <QString>
#include <QPointF>
#include "mozilla/embedlite/EmbedLiteView.h"

class QGraphicsView;
class QTouchEvent;
class QGraphicsMozView;
class QMozContext;

class QGraphicsMozViewPrivate : public mozilla::embedlite::EmbedLiteViewListener
{
public:
    QGraphicsMozViewPrivate(QGraphicsMozView* view);
    virtual ~QGraphicsMozViewPrivate();

    QGraphicsView* GetViewWidget();
    void ReceiveInputEvent(const mozilla::InputData& event);
    void touchEvent(QTouchEvent* event);
    void UpdateViewSize();
    virtual bool RequestCurrentGLContext();
    virtual void ViewInitialized();
    virtual void SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    virtual bool Invalidate();
    virtual void OnLocationChanged(const char* aLocation, bool aCanGoBack, bool aCanGoForward);
    virtual void OnLoadProgress(int32_t aProgress, int32_t aCurTotal, int32_t aMaxTotal);
    virtual void OnLoadStarted(const char* aLocation);
    virtual void OnLoadFinished(void);

    // View finally destroyed and deleted
    virtual void ViewDestroyed();
    virtual void RecvAsyncMessage(const PRUnichar* aMessage, const PRUnichar* aData);
    virtual char* RecvSyncMessage(const PRUnichar* aMessage, const PRUnichar*  aData);
    virtual void OnLoadRedirect(void);
    virtual void OnSecurityChanged(const char* aStatus, unsigned int aState);
    virtual void OnFirstPaint(int32_t aX, int32_t aY);
    virtual void IMENotification(int aIstate, bool aOpen, int aCause, int aFocusChange, const PRUnichar* inputType, const PRUnichar* inputMode);

    virtual void OnScrolledAreaChanged(unsigned int aWidth, unsigned int aHeight);
    virtual void OnScrollChanged(int32_t offSetX, int32_t offSetY);
    virtual void OnTitleChanged(const PRUnichar* aTitle);
    virtual void SetFirstPaintViewport(const nsIntPoint& aOffset, float aZoom,
                                       const nsIntRect& aPageRect, const gfxRect& aCssPageRect);
    virtual void SyncViewportInfo(const nsIntRect& aDisplayPort,
                                  float aDisplayResolution, bool aLayersUpdated,
                                  nsIntPoint& aScrollOffset, float& aScaleX, float& aScaleY);
    virtual void SetPageRect(const gfxRect& aCssPageRect);
    virtual bool SendAsyncScrollDOMEvent(const gfxRect& aContentRect, const gfxSize& aScrollableSize);
    virtual bool ScrollUpdate(const gfxPoint& aPosition, const float aResolution);
    virtual bool HandleLongTap(const nsIntPoint& aPoint);
    virtual bool HandleSingleTap(const nsIntPoint& aPoint);
    virtual bool HandleDoubleTap(const nsIntPoint& aPoint);

    QGraphicsMozView* q;
    QMozContext* mContext;
    mozilla::embedlite::EmbedLiteView* mView;
    bool mViewInitialized;
    QColor mBgColor;
    QImage mTempBufferImage;
    QSize mSize;
    QTime mTouchTime;
    bool mPendingTouchEvent;
    QTime mPanningTime;
    QString mLocation;
    QString mTitle;
    int mProgress;
    bool mCanGoBack;
    bool mCanGoForward;
    bool mIsLoading;
    bool mLastIsGoodRotation;
    bool mIsPasswordField;
    bool mGraphicsViewAssigned;
    QRect mContentRect;
    QSize mScrollableSize;
    QPointF mScrollableOffset;
    float mContentResolution;
    bool mIsPainted;
};

#endif /* qgraphicsmozview_p_h */
