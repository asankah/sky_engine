// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of fitness;

typedef void FitnessItemHandler(FitnessItem item);

const double kFitnessItemHeight = 79.0;

abstract class FitnessItem {
  FitnessItem({ this.when }) {
    assert(when != null);
  }
  final DateTime when;

  // TODO(jackson): Internationalize
  String get displayDate => "${when.year.toString()}-${when.month.toString().padLeft(2,'0')}-${when.day.toString().padLeft(2,'0')}";

  FitnessItemRow toRow({ FitnessItemHandler onDismissed });
}

abstract class FitnessItemRow extends Component {

  FitnessItemRow({ FitnessItem item, this.onDismissed })
   : this.item = item,
     super(key: new Key(item.when.toString())) {
    assert(onDismissed != null);
  }

  final FitnessItem item;
  final FitnessItemHandler onDismissed;

  Widget buildContent();

  Widget build() {
    return new Dismissable(
      onDismissed: () => onDismissed(item),
      child: new Card(
        child: new Container(
          height: kFitnessItemHeight,
          padding: const EdgeDims.all(8.0),
          child: buildContent()
        )
      )
    );
  }
}
