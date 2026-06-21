# ADR 0002 — Injecting the QML singleton without `setContextProperty`

**Status:** accepted

Goal #3 forbids `setContextProperty`. But the QML-facing façade (`AppContext`) needs services that
are **constructed at runtime** in the composition root. `QML_FOREIGN + QML_SINGLETON` alone **cannot**
inject a runtime instance (it default-constructs). The two legal options are:

- **(a)** `qmlRegisterSingletonInstance(uri, maj, min, "AppContext", &instance)` — imperative, typed.
- **(b)** a `QML_SINGLETON` whose static `create(QQmlEngine*, QJSEngine*)` returns a
  composition-root instance via a set-once global.

**Decision: use (b).** `AppContext` is declared `QML_ELEMENT QML_SINGLETON` with a static `create()`
that returns `AppContext::s_instance` (set by `main()` before `engine.loadFromModule`), marked
`QJSEngine::CppOwnership` so QML never deletes it. This is:

- **fully declarative** — `qmllint` / `qmlls` see the type (unlike `setContextProperty`);
- **single-module** — no multi-module singleton re-export, so no transitive-import footgun;
- **injected** — the real services are wired in the composition root, not `getInstance()`.

`AppContext` stays a **thin pointer-holder** exposing typed models, rather than re-exposing
WatchFlower's large ~660-binding context object. Option (a) remains acceptable where a
`create()` hook is awkward.

**Gotcha found in Milestone A — the singleton must be non-default-constructible.** Option (b) only
works if the engine actually calls `create()`. If the class *also* has a usable default constructor,
the QML engine silently **default-constructs its own instance** instead of calling `create()` —
yielding an un-wired `AppContext` (null services). Symptom: the Scan button did nothing, no logs, no
status — QML was talking to a *different* object than the composition root had wired. **Fix:** give
`AppContext` a single constructor that *requires* the services (constructor injection) and **no**
default constructor, so `create()` is the only way QML can obtain the instance. The composition root
builds it and assigns `s_instance` before `engine.loadFromModule`.

See `src/gui/appcontext.h` / `appcontext.cpp` and the composition root in `src/gui/main.cpp`.
