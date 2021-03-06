// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file./*

#include "sky/engine/core/editing/PositionWithAffinity.h"

namespace blink {

PositionWithAffinity::PositionWithAffinity(const Position& position, EAffinity affinity)
    : m_position(position)
    , m_affinity(affinity)
{
}

PositionWithAffinity::PositionWithAffinity()
    : m_affinity(DOWNSTREAM)
{
}

PositionWithAffinity::~PositionWithAffinity()
{
}

} // namespace blink
