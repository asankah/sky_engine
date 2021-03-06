// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:sky/animation/animated_value.dart';
import 'package:sky/animation/forces.dart';
import 'package:sky/animation/timeline.dart';

enum AnimationDirection {
  forward,
  reverse
}

enum AnimationStatus {
  dismissed, // stoped at 0
  forward,   // animating from 0 => 1
  reverse,   // animating from 1 => 0
  completed, // stopped at 1
}

// This class manages a "performance" - a collection of values that change
// based on a timeline. For example, a performance may handle an animation
// of a menu opening by sliding and fading in (changing Y value and opacity)
// over .5 seconds. The performance can move forwards (present) or backwards
// (dismiss). A consumer may also take direct control of the timeline by
// manipulating |progress|, or |fling| the timeline causing a physics-based
// simulation to take over the progression.
class AnimationPerformance {
  AnimationPerformance({this.variable, this.duration}) {
    _timeline = new Timeline(_tick);
  }

  AnimatedVariable variable;
  Duration duration;

  // Advances from 0 to 1. On each tick, we'll update our variable's values.
  Timeline _timeline;
  Timeline get timeline => _timeline;

  AnimationDirection _direction;
  AnimationDirection get direction => _direction;

  double get progress => timeline.value;
  void set progress(double t) {
    // TODO(mpcomplete): should this affect |direction|?
    stop();
    timeline.value = t.clamp(0.0, 1.0);
    _checkStatusChanged();
  }

  bool get isDismissed => status == AnimationStatus.dismissed;
  bool get isCompleted => status == AnimationStatus.completed;
  bool get isAnimating => timeline.isAnimating;

  AnimationStatus get status {
    if (!isAnimating && progress == 1.0)
      return AnimationStatus.completed;
    if (!isAnimating && progress == 0.0)
      return AnimationStatus.dismissed;
    return direction == AnimationDirection.forward ?
        AnimationStatus.forward :
        AnimationStatus.reverse;
  }

  Future play([AnimationDirection direction = AnimationDirection.forward]) {
    _direction = direction;
    return resume();
  }
  Future forward() => play(AnimationDirection.forward);
  Future reverse() => play(AnimationDirection.reverse);
  Future resume() => _animateTo(direction == AnimationDirection.forward ? 1.0 : 0.0);

  void stop() {
    timeline.stop();
  }

  // Flings the timeline with an optional force (defaults to a critically damped
  // spring) and initial velocity. Negative velocity causes the timeline to go
  // in reverse.
  Future fling({double velocity: 1.0, Force force}) {
    if (force == null)
      force = kDefaultSpringForce;
    // This is an approximation - the force may not necessarily result in
    // animating the same direction as the initial velocity.
    _direction = velocity >= 0.0 ? AnimationDirection.forward : AnimationDirection.reverse;
    return timeline.fling(force.release(progress, velocity));
  }

  final List<Function> _listeners = new List<Function>();

  void addListener(Function listener) {
    _listeners.add(listener);
  }

  void removeListener(Function listener) {
    _listeners.remove(listener);
  }

  void _notifyListeners() {
    List<Function> localListeners = new List<Function>.from(_listeners);
    for (Function listener in localListeners)
      listener();
  }

  final List<Function> _statusListeners = new List<Function>();

  void addStatusListener(Function listener) {
    _statusListeners.add(listener);
  }

  void removeStatusListener(Function listener) {
    _statusListeners.remove(listener);
  }

  AnimationStatus _lastStatus = AnimationStatus.dismissed;
  void _checkStatusChanged() {
    AnimationStatus currentStatus = status;
    if (currentStatus != _lastStatus) {
      List<Function> localListeners = new List<Function>.from(_statusListeners);
      for (Function listener in localListeners)
        listener(currentStatus);
    }
    _lastStatus = currentStatus;
  }

  Future _animateTo(double target) {
    double remainingDistance = (target - timeline.value).abs();
    timeline.stop();
    if (remainingDistance == 0.0)
      return new Future.value();
    return timeline.animateTo(target, duration: duration * remainingDistance);
  }

  void _tick(double t) {
    variable.setProgress(t);
    _notifyListeners();
    _checkStatusChanged();
  }
}
