// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "providerdescriptor.h"

using namespace klr;

// The per-provider onboarding table (ADR 0027): the single source of truth the AI settings screen
// reads from. The point of these tests is that a wrong endpoint or key URL is a failing build, not
// a shipped bug — so the cloud endpoints are asserted exactly. Model lists are asserted by
// invariant (default ∈ list, non-empty), not by churning exact strings.
class TestProviderDescriptor : public QObject {
    Q_OBJECT

    // The provider-factory branch order (SettingsStore::agentProviderType).
    enum { OpenAiCompat = 0, Responses = 1, Anthropic = 2, Gemini = 3, Count = 4 };

private slots:
    void cloudEndpointsAreExactAndFixed()
    {
        // The dialects append /responses, /messages, /models/{model}:… — so these bases must be
        // exact, or every cloud request 404s.
        QCOMPARE(providerDescriptor(Responses).fixedEndpoint,
                 QStringLiteral("https://api.openai.com/v1"));
        QCOMPARE(providerDescriptor(Anthropic).fixedEndpoint,
                 QStringLiteral("https://api.anthropic.com/v1"));
        QCOMPARE(providerDescriptor(Gemini).fixedEndpoint,
                 QStringLiteral("https://generativelanguage.googleapis.com/v1beta"));
    }

    void onlyOpenAiCompatHasUserEndpoint()
    {
        // The BYO-endpoint escape hatch is the only branch without a fixed endpoint.
        QVERIFY(providerDescriptor(OpenAiCompat).fixedEndpoint.isEmpty());
        for (int t : { Responses, Anthropic, Gemini })
            QVERIFY(!providerDescriptor(t).fixedEndpoint.isEmpty());
    }

    void keyUrlPresentExactlyWhenKeyNeeded()
    {
        for (int t = 0; t < Count; ++t) {
            const ProviderDescriptor &d = providerDescriptor(t);
            QCOMPARE(!d.keyUrl.isEmpty(), d.needsKey);
        }
        QVERIFY(!providerDescriptor(OpenAiCompat).needsKey); // local can be keyless
        QVERIFY(providerDescriptor(Gemini).needsKey);
        QCOMPARE(providerDescriptor(Gemini).keyUrl,
                 QStringLiteral("https://aistudio.google.com/apikey"));
    }

    void defaultModelIsAmongKnownModels()
    {
        for (int t = 0; t < Count; ++t) {
            const ProviderDescriptor &d = providerDescriptor(t);
            QVERIFY(!d.displayName.isEmpty());
            QVERIFY(!d.defaultModel.isEmpty());
            QVERIFY2(d.knownModels.contains(d.defaultModel),
                     qPrintable(QStringLiteral("default %1 not in knownModels for %2")
                                    .arg(d.defaultModel, d.displayName)));
        }
    }

    void textOnlyModelsAreASubsetOfKnown()
    {
        // The capability-warning set must reference real (suggestable) models, else the warning
        // could never fire from the combo. The local-Ollama default IS text-only — the realistic
        // "vision on + stock model" nudge.
        for (int t = 0; t < Count; ++t) {
            const ProviderDescriptor &d = providerDescriptor(t);
            for (const QString &m : d.textOnlyModels)
                QVERIFY2(d.knownModels.contains(m),
                         qPrintable(QStringLiteral("text-only %1 not in knownModels").arg(m)));
        }
        QVERIFY(providerDescriptor(OpenAiCompat)
                    .textOnlyModels.contains(providerDescriptor(OpenAiCompat).defaultModel));
    }

    void onlyGeminiCarriesAFreeTierNote()
    {
        QVERIFY(!providerDescriptor(Gemini).freeTierUrl.isEmpty());
        for (int t : { OpenAiCompat, Responses, Anthropic })
            QVERIFY(providerDescriptor(t).freeTierUrl.isEmpty());
    }

    void outOfRangeClampsToDefault()
    {
        // Mirrors the factory's default: branch — a stray index must not read out of bounds.
        QCOMPARE(providerDescriptor(-1).displayName, providerDescriptor(OpenAiCompat).displayName);
        QCOMPARE(providerDescriptor(99).displayName, providerDescriptor(OpenAiCompat).displayName);
    }
};

QTEST_GUILESS_MAIN(TestProviderDescriptor)
#include "test_providerdescriptor.moc"
