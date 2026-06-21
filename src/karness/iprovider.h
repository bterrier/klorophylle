// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "inferencerequest.h"
#include "modelcaps.h"
#include "streamevent.h"

#include <QtCore/QFuture>

namespace karness {

// The pluggable inference backend (docs/adr/0019 decision 3). One dialect
// implementation per provider API; hosts and the agent loop see only this.
//
// Streaming contract (binding for every implementation):
// - generate() returns a QFuture fed from a QPromise<StreamEvent>:
//   start() -> one addResult() per event -> exactly ONE terminal event,
//   Done XOR ErrorEvent -> finish(). finish() must run on EVERY exit path
//   (a destroyed unfinished promise leaves the future hanging forever).
// - Errors travel as ErrorEvent results, NEVER QPromise::setException.
// - cancel() aborts the underlying transport; QFuture::cancel() does not.
//   A cancelled turn terminates with ErrorEvent{Code::Cancelled}.
// - isReady() false means no usable model/endpoint; callers fall through
//   to the next provider rather than calling generate().
class IProvider {
public:
    virtual ~IProvider() = default;

    virtual ModelCaps caps() const = 0;
    virtual bool isReady() const = 0;
    virtual QFuture<StreamEvent> generate(const InferenceRequest &request) = 0;
    virtual void cancel() = 0;
};

} // namespace karness
