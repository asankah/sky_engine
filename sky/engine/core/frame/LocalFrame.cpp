/*
 * Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>
 *                     1999 Lars Knoll <knoll@kde.org>
 *                     1999 Antti Koivisto <koivisto@kde.org>
 *                     2000 Simon Hausmann <hausmann@kde.org>
 *                     2000 Stefan Schimanski <1Stein@gmx.de>
 *                     2001 George Staikos <staikos@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2005 Alexey Proskuryakov <ap@nypop.com>
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008 Google Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "sky/engine/core/frame/LocalFrame.h"

#include "gen/sky/platform/RuntimeEnabledFeatures.h"
#include "sky/engine/core/dom/Range.h"
#include "sky/engine/core/editing/htmlediting.h"
#include "sky/engine/core/editing/RenderedPosition.h"
#include "sky/engine/core/editing/VisiblePosition.h"
#include "sky/engine/core/events/Event.h"
#include "sky/engine/core/frame/FrameDestructionObserver.h"
#include "sky/engine/core/frame/FrameHost.h"
#include "sky/engine/core/frame/FrameView.h"
#include "sky/engine/core/frame/LocalDOMWindow.h"
#include "sky/engine/core/frame/Settings.h"
#include "sky/engine/core/loader/FrameLoaderClient.h"
#include "sky/engine/core/page/Page.h"
#include "sky/engine/core/rendering/HitTestResult.h"
#include "sky/engine/core/rendering/RenderLayer.h"
#include "sky/engine/core/rendering/RenderView.h"
#include "sky/engine/core/script/dart_controller.h"
#include "sky/engine/platform/graphics/GraphicsContext.h"
#include "sky/engine/platform/graphics/ImageBuffer.h"
#include "sky/engine/platform/text/TextStream.h"
#include "sky/engine/wtf/PassOwnPtr.h"
#include "sky/engine/wtf/StdLibExtras.h"

namespace blink {

inline LocalFrame::LocalFrame(FrameLoaderClient* client, FrameHost* host)
    : Frame(client, host)
    , m_deprecatedLoader(this)
    , m_document(nullptr)
{
    if (page())
        page()->setMainFrame(this);
}

PassRefPtr<LocalFrame> LocalFrame::create(FrameLoaderClient* client, FrameHost* host)
{
    return adoptRef(new LocalFrame(client, host));
}

LocalFrame::~LocalFrame()
{
    setView(nullptr);
    m_deprecatedLoader.clear();
    setDOMWindow(nullptr);

    // FIXME: What to do here... some of this is redundant with ~Frame.
    HashSet<FrameDestructionObserver*>::iterator stop = m_destructionObservers.end();
    for (HashSet<FrameDestructionObserver*>::iterator it = m_destructionObservers.begin(); it != stop; ++it)
        (*it)->frameDestroyed();
}

FrameLoaderClient* LocalFrame::loaderClient() const
{
    return static_cast<FrameLoaderClient*>(client());
}

void LocalFrame::detach()
{
    // A lot of the following steps can result in the current frame being
    // detached, so protect a reference to it.
    RefPtr<LocalFrame> protect(this);
    m_deprecatedLoader.stopAllLoaders();
    m_deprecatedLoader.closeURL();
    detachChildren();
    // stopAllLoaders() needs to be called after detachChildren(), because detachChildren()
    // will trigger the unload event handlers of any child frames, and those event
    // handlers might start a new subresource load in this frame.
    m_deprecatedLoader.stopAllLoaders();

    setView(nullptr);
    willDetachFrameHost();

    // After this, we must no longer talk to the client since this clears
    // its owning reference back to our owning LocalFrame.
    loaderClient()->detachedFromParent();
    clearClient();
    detachFromFrameHost();
}

void LocalFrame::setView(PassRefPtr<FrameView> view)
{
    // We the custom scroll bars as early as possible to prevent m_doc->detach()
    // from messing with the view such that its scroll bars won't be torn down.
    // FIXME: We should revisit this.
    if (m_view)
        m_view->prepareForDetach();

    // Prepare for destruction now, so any unload event handlers get run and the LocalDOMWindow is
    // notified. If we wait until the view is destroyed, then things won't be hooked up enough for
    // these calls to work.
    if (!view && document() && document()->isActive()) {
        // FIXME: We don't call willRemove here. Why is that OK?
        document()->prepareForDestruction();
    }

    m_view = view;
}

FloatSize LocalFrame::resizePageRectsKeepingRatio(const FloatSize& originalSize, const FloatSize& expectedSize)
{
    FloatSize resultSize;
    if (!contentRenderer())
        return FloatSize();

    ASSERT(fabs(originalSize.width()) > std::numeric_limits<float>::epsilon());
    float ratio = originalSize.height() / originalSize.width();
    resultSize.setWidth(floorf(expectedSize.width()));
    resultSize.setHeight(floorf(resultSize.width() * ratio));
    return resultSize;
}

void LocalFrame::setDOMWindow(PassRefPtr<LocalDOMWindow> domWindow)
{
    Frame::setDOMWindow(domWindow);
}

void LocalFrame::addDestructionObserver(FrameDestructionObserver* observer)
{
    m_destructionObservers.add(observer);
}

void LocalFrame::removeDestructionObserver(FrameDestructionObserver* observer)
{
    m_destructionObservers.remove(observer);
}

void LocalFrame::willDetachFrameHost()
{
    // We should never be detatching the page during a Layout.
    RELEASE_ASSERT(!m_view || !m_view->isInPerformLayout());

    HashSet<FrameDestructionObserver*>::iterator stop = m_destructionObservers.end();
    for (HashSet<FrameDestructionObserver*>::iterator it = m_destructionObservers.begin(); it != stop; ++it)
        (*it)->willDetachFrameHost();
}

void LocalFrame::detachFromFrameHost()
{
    // We should never be detatching the page during a Layout.
    RELEASE_ASSERT(!m_view || !m_view->isInPerformLayout());
    m_host = 0;
}

VisiblePosition LocalFrame::visiblePositionForPoint(const IntPoint& framePoint)
{
    if (!contentRenderer() || !view() || !view()->didFirstLayout())
      return VisiblePosition();

    LayoutSize padding = LayoutSize();
    HitTestResult result(framePoint, padding.height(), padding.width(), padding.height(), padding.width());
    HitTestRequest request(HitTestRequest::ReadOnly | HitTestRequest::Active);
    contentRenderer()->hitTest(request, result);

    Node* node = result.innerNonSharedNode();
    if (!node)
        return VisiblePosition();
    RenderObject* renderer = node->renderer();
    if (!renderer)
        return VisiblePosition();
    VisiblePosition visiblePos = VisiblePosition(renderer->positionForPoint(result.localPoint()));
    if (visiblePos.isNull())
        visiblePos = VisiblePosition(firstPositionInOrBeforeNode(node));
    return visiblePos;
}

RenderView* LocalFrame::contentRenderer() const
{
    return document() ? document()->renderView() : 0;
}

void LocalFrame::setDocument(Document* document)
{
    m_document = document;
}

Document* LocalFrame::document() const
{
    if (m_document)
        return m_document;
    return m_domWindow ? m_domWindow->document() : 0;
}

IntRect firstRectForRange(Range* range)
{
    LayoutUnit extraWidthToEndOfLine = 0;
    ASSERT(range->startContainer());
    ASSERT(range->endContainer());

    IntRect startCaretRect = RenderedPosition(VisiblePosition(range->startPosition()).deepEquivalent(), DOWNSTREAM).absoluteRect(&extraWidthToEndOfLine);
    if (startCaretRect == LayoutRect())
        return IntRect();

    IntRect endCaretRect = RenderedPosition(VisiblePosition(range->endPosition()).deepEquivalent(), UPSTREAM).absoluteRect();
    if (endCaretRect == LayoutRect())
        return IntRect();

    if (startCaretRect.y() == endCaretRect.y()) {
        // start and end are on the same line
        return IntRect(std::min(startCaretRect.x(), endCaretRect.x()),
            startCaretRect.y(),
            abs(endCaretRect.x() - startCaretRect.x()),
            std::max(startCaretRect.height(), endCaretRect.height()));
    }

    // start and end aren't on the same line, so go from start to the end of its line
    return IntRect(startCaretRect.x(),
        startCaretRect.y(),
        startCaretRect.width() + extraWidthToEndOfLine,
        startCaretRect.height());
}

PassRefPtr<Range> LocalFrame::rangeForPoint(const IntPoint& framePoint)
{
    VisiblePosition position = visiblePositionForPoint(framePoint);
    if (position.isNull())
        return nullptr;

    VisiblePosition previous = position.previous();
    if (previous.isNotNull()) {
        RefPtr<Range> previousCharacterRange = makeRange(previous, position);
        LayoutRect rect = firstRectForRange(previousCharacterRange.get());
        if (rect.contains(framePoint))
            return previousCharacterRange.release();
    }

    VisiblePosition next = position.next();
    if (RefPtr<Range> nextCharacterRange = makeRange(position, next)) {
        LayoutRect rect = firstRectForRange(nextCharacterRange.get());
        if (rect.contains(framePoint))
            return nextCharacterRange.release();
    }

    return nullptr;
}

void LocalFrame::createView(const IntSize& viewportSize, const Color& backgroundColor, bool transparent)
{
    ASSERT(this);

    setView(nullptr);

    RefPtr<FrameView> frameView;
    frameView = FrameView::create(this, viewportSize);

    // The layout size is set by WebViewImpl to support @viewport
    frameView->setLayoutSizeFixedToFrameSize(false);

    setView(frameView);

    frameView->updateBackgroundRecursively(backgroundColor, transparent);
}

void LocalFrame::deviceOrPageScaleFactorChanged()
{
    document()->mediaQueryAffectingValueChanged();
}

double LocalFrame::devicePixelRatio() const
{
    if (!m_host)
        return 0;
    return m_host->deviceScaleFactor();
}

} // namespace blink
