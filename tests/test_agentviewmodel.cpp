// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "agentviewmodel.h"
#include "clock.h"
#include "fakeprovider.h"
#include "inmemoryagentrepository.h"
#include "inmemoryattachmentfilestore.h"
#include "inmemoryattachmentrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemorykeyvaluestore.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysecretstore.h"
#include "attachment.h"
#include "fakewebfetcher.h"
#include "journalentry.h"
#include "plant.h"
#include "settingsstore.h"

using namespace klr;
using namespace karness;

namespace {

StreamEvent doneText(const QString &text)
{
    return Done{ Message{ Role::Assistant, { TextBlock{ text } } }, StopReason::EndTurn, std::nullopt };
}

StreamEvent doneCall(const QString &id, const QString &name, const QJsonObject &args)
{
    return Done{ Message{ Role::Assistant, { ToolCallBlock{ id, name, args } } },
                 StopReason::ToolCalls, std::nullopt };
}

// An assistant turn that carries reasoning before its answer (the order blocks render in).
StreamEvent doneReasoning(const QString &reasoning, const QString &text)
{
    return Done{ Message{ Role::Assistant,
                          { ReasoningBlock{ reasoning, {} }, TextBlock{ text } } },
                 StopReason::EndTurn, std::nullopt };
}

Plant makePlant()
{
    Plant p;
    p.id = PlantId::generate();
    p.displayName = QStringLiteral("Basil");
    p.species = QStringLiteral("Ocimum basilicum");
    p.trackedSince = QDateTime::currentDateTimeUtc();
    return p;
}

// All the injected collaborators a view-model needs, plus the captured FakeProvider.
struct Harness {
    InMemoryPlantRepository plants;
    InMemoryJournalRepository journal;
    InMemoryBindingRepository bindings;
    InMemoryReadingRepository readings;
    InMemoryCareThresholdRepository thresholds;
    InMemoryAgentRepository transcripts;
    InMemoryAttachmentRepository attachments;
    InMemoryAttachmentFileStore fileStore;
    InMemoryKeyValueStore kv;
    InMemorySecretStore secrets;
    FakeWebFetcher webFetcher;
    FakeClock clock;
    SettingsStore settings{ &kv };
    QList<FakeProvider::ScriptedTurn> script;
    FakeProvider *provider = nullptr;

    std::unique_ptr<AgentViewModel> makeVm()
    {
        auto factory = [this](int /*providerType*/,
                              const ProviderConfig &) -> std::unique_ptr<IProvider> {
            auto p = std::make_unique<FakeProvider>();
            p->setScript(script);
            provider = p.get();
            return p;
        };
        return std::make_unique<AgentViewModel>(plants, journal, bindings, readings, thresholds,
                                                clock, settings, secrets, transcripts, webFetcher,
                                                attachments, fileStore, std::move(factory));
    }

    QList<AgentMessageRecord> persisted() const
    {
        const QList<AgentConversation> convs = transcripts.conversations();
        return convs.isEmpty() ? QList<AgentMessageRecord>{}
                               : transcripts.messagesFor(convs.first().id);
    }
};

QString rowText(const AgentViewModel &vm, int row)
{
    return vm.data(vm.index(row), AgentViewModel::TextRole).toString();
}

int rowKind(const AgentViewModel &vm, int row)
{
    return vm.data(vm.index(row), AgentViewModel::KindRole).toInt();
}

} // namespace

// Drives the AgentViewModel against a scripted FakeProvider (injected via the factory):
// streaming text, the tool-call loop, transcript persistence, and the confirmation gate.
class TestAgentViewModel : public QObject {
    Q_OBJECT

private slots:
    void textTurnRendersAndPersists()
    {
        Harness h;
        h.script = { { { TextDelta{ QStringLiteral("Your ") }, TextDelta{ QStringLiteral("basil.") },
                        doneText(QStringLiteral("Your basil.")) } } };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("how is my basil?"));
        QTRY_VERIFY(!vm->busy());

        QCOMPARE(vm->rowCount(), 2);
        QCOMPARE(rowKind(*vm, 0), int(AgentViewModel::UserKind));
        QCOMPARE(rowText(*vm, 0), QStringLiteral("how is my basil?"));
        QCOMPARE(rowKind(*vm, 1), int(AgentViewModel::AssistantKind));
        QCOMPARE(rowText(*vm, 1), QStringLiteral("Your basil."));

        // user + assistant persisted to the transcript.
        QCOMPARE(h.persisted().size(), 2);
    }

    void toolCallLoopRendersRows()
    {
        Harness h;
        h.plants.add(makePlant());
        h.script = {
            { { doneCall(QStringLiteral("c1"), QStringLiteral("list_plants"), {}) } },
            { { doneText(QStringLiteral("You have one plant.")) } },
        };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("what do I grow?"));
        QTRY_VERIFY(!vm->busy());

        bool sawToolCall = false;
        bool sawToolResult = false;
        for (int i = 0; i < vm->rowCount(); ++i) {
            if (rowKind(*vm, i) == int(AgentViewModel::ToolCallKind))
                sawToolCall = true;
            if (rowKind(*vm, i) == int(AgentViewModel::ToolResultKind))
                sawToolResult = true;
        }
        QVERIFY(sawToolCall);
        QVERIFY(sawToolResult);
        QCOMPARE(rowKind(*vm, vm->rowCount() - 1), int(AgentViewModel::AssistantKind));
        QCOMPARE(rowText(*vm, vm->rowCount() - 1), QStringLiteral("You have one plant."));
    }

    void confirmationApproveWritesJournalEntry()
    {
        Harness h;
        const Plant p = makePlant();
        h.plants.add(p);
        h.script = {
            { { doneCall(QStringLiteral("c1"), QStringLiteral("add_journal_entry"),
                         QJsonObject{ { QStringLiteral("plant_id"), p.id.toString() },
                                      { QStringLiteral("note"), QStringLiteral("watered") } }) } },
            { { doneText(QStringLiteral("Logged it.")) } },
        };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("log that I watered the basil"));
        QTRY_VERIFY(vm->pendingConfirmation());
        QVERIFY(h.journal.forPlant(p.id).isEmpty()); // nothing written before approval

        vm->confirm(true);
        QTRY_VERIFY(!vm->busy());

        const QList<JournalEntry> entries = h.journal.forPlant(p.id);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().note, QStringLiteral("watered"));
        QVERIFY(!vm->pendingConfirmation());
    }

    void confirmationRejectWritesNothing()
    {
        Harness h;
        const Plant p = makePlant();
        h.plants.add(p);
        h.script = {
            { { doneCall(QStringLiteral("c1"), QStringLiteral("add_journal_entry"),
                         QJsonObject{ { QStringLiteral("plant_id"), p.id.toString() },
                                      { QStringLiteral("note"), QStringLiteral("watered") } }) } },
            { { doneText(QStringLiteral("Okay, not logged.")) } },
        };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("log watering"));
        QTRY_VERIFY(vm->pendingConfirmation());

        vm->confirm(false);
        QTRY_VERIFY(!vm->busy());

        QVERIFY(h.journal.forPlant(p.id).isEmpty()); // declined -> no write
    }

    // Two add_journal_entry calls in ONE assistant message: the user is prompted for each in
    // turn (FIFO) and both writes land — neither call is silently dropped.
    void parallelConfirmationsPromptForEach()
    {
        Harness h;
        const Plant p = makePlant();
        h.plants.add(p);
        const Message twoCalls{
            Role::Assistant,
            { ToolCallBlock{ QStringLiteral("c1"), QStringLiteral("add_journal_entry"),
                             QJsonObject{ { QStringLiteral("plant_id"), p.id.toString() },
                                          { QStringLiteral("note"), QStringLiteral("watered") } } },
              ToolCallBlock{ QStringLiteral("c2"), QStringLiteral("add_journal_entry"),
                             QJsonObject{ { QStringLiteral("plant_id"), p.id.toString() },
                                          { QStringLiteral("note"), QStringLiteral("fertilized") } } } }
        };
        h.script = {
            { { Done{ twoCalls, StopReason::ToolCalls, std::nullopt } } },
            { { doneText(QStringLiteral("Logged both.")) } },
        };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("log that I watered and fertilized the basil"));
        QTRY_VERIFY(vm->pendingConfirmation());
        QVERIFY(h.journal.forPlant(p.id).isEmpty()); // nothing written before approval

        vm->confirm(true);                            // approve the first
        QTRY_VERIFY(vm->pendingConfirmation());       // second prompt surfaces
        QCOMPARE(h.journal.forPlant(p.id).size(), 1); // first write landed

        vm->confirm(true);                            // approve the second
        QTRY_VERIFY(!vm->busy());

        const QList<JournalEntry> entries = h.journal.forPlant(p.id);
        QCOMPARE(entries.size(), 2);
        QVERIFY(!vm->pendingConfirmation());
    }

    // set_plant_memory is registered alongside the domain tools but, unlike add_journal_entry, is
    // NOT wrapped in the confirmation decorator — a memory write executes without a user prompt
    // (ADR 0021 decision 3). The system prompt names the tool when tools are on.
    void memoryToolRegisteredUnconfirmedAndPromptMentionsIt()
    {
        Harness h;
        const Plant p = makePlant();
        h.plants.add(p);
        h.script = {
            { { doneCall(QStringLiteral("c1"), QStringLiteral("set_plant_memory"),
                         QJsonObject{ { QStringLiteral("plant_id"), p.id.toString() },
                                      { QStringLiteral("text"),
                                        QStringLiteral("waters lightly") } }) } },
            { { doneText(QStringLiteral("Noted.")) } },
        };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("remember that I water the basil lightly"));
        QTRY_VERIFY(!vm->busy());

        // Never gated — the memory write ran straight through (no ConfirmingTool wrapper).
        QVERIFY(!vm->pendingConfirmation());
        const QList<JournalEntry> entries = h.journal.forPlant(p.id);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().kind, JournalEntryKind::Memory);
        QCOMPARE(entries.first().note, QStringLiteral("waters lightly"));

        // The tool was advertised to the provider, and the system prompt points the model at it.
        QVERIFY(!h.provider->requests().isEmpty());
        const InferenceRequest &req = h.provider->requests().first();
        bool advertised = false;
        for (const ToolSpec &t : req.tools)
            if (t.name == QStringLiteral("set_plant_memory"))
                advertised = true;
        QVERIFY(advertised);
        QVERIFY(!req.messages.isEmpty());
        QCOMPARE(req.messages.first().role, Role::System);
        const QString systemText =
            std::get<TextBlock>(req.messages.first().blocks.first()).text;
        QVERIFY(systemText.contains(QStringLiteral("set_plant_memory")));
    }

    // set_global_memory + read_global_memory are registered alongside the plant-memory tools and, like
    // set_plant_memory, the write is NOT wrapped in the confirmation decorator (ADR 0022 decision 2).
    // The system prompt names both tools when tools are on.
    void globalMemoryToolsRegisteredUnconfirmedAndPromptMentionsThem()
    {
        Harness h;
        h.script = {
            { { doneCall(QStringLiteral("c1"), QStringLiteral("set_global_memory"),
                         QJsonObject{ { QStringLiteral("text"),
                                        QStringLiteral("hard tap water") } }) } },
            { { doneText(QStringLiteral("Noted.")) } },
        };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("remember that we have hard tap water"));
        QTRY_VERIFY(!vm->busy());

        // Never gated — the global memory write ran straight through (no ConfirmingTool wrapper).
        QVERIFY(!vm->pendingConfirmation());
        const QList<JournalEntry> entries = h.journal.globalEntries();
        QCOMPARE(entries.size(), 1);
        QVERIFY(!entries.first().plant.has_value()); // plant-less (global)
        QCOMPARE(entries.first().kind, JournalEntryKind::Memory);
        QCOMPARE(entries.first().note, QStringLiteral("hard tap water"));

        // Both tools advertised, and the system prompt points the model at them.
        QVERIFY(!h.provider->requests().isEmpty());
        const InferenceRequest &req = h.provider->requests().first();
        bool advertisedSet = false, advertisedRead = false;
        for (const ToolSpec &t : req.tools) {
            if (t.name == QStringLiteral("set_global_memory"))
                advertisedSet = true;
            if (t.name == QStringLiteral("read_global_memory"))
                advertisedRead = true;
        }
        QVERIFY(advertisedSet);
        QVERIFY(advertisedRead);
        const QString systemText =
            std::get<TextBlock>(req.messages.first().blocks.first()).text;
        QVERIFY(systemText.contains(QStringLiteral("set_global_memory")));
        QVERIFY(systemText.contains(QStringLiteral("read_global_memory")));
    }

    // read_online_plant_db is opt-in — registered only when agentWebToolEnabled is on, and the
    // system prompt names it only then (ADR 0023 decision 6).
    void webToolOnlyRegisteredWhenEnabled()
    {
        auto advertisesWebTool = [](const InferenceRequest &req) {
            for (const ToolSpec &t : req.tools)
                if (t.name == QStringLiteral("read_online_plant_db"))
                    return true;
            return false;
        };

        // Off by default: tools are on, but the web tool is not advertised and not named.
        {
            Harness h;
            QVERIFY(!h.settings.agentWebToolEnabled());
            h.script = { { { doneText(QStringLiteral("ok")) } } };
            auto vm = h.makeVm();
            vm->sendMessage(QStringLiteral("hi"));
            QTRY_VERIFY(!vm->busy());
            const InferenceRequest &req = h.provider->requests().first();
            QVERIFY(!advertisesWebTool(req));
            QVERIFY(!std::get<TextBlock>(req.messages.first().blocks.first())
                         .text.contains(QStringLiteral("read_online_plant_db")));
        }

        // Enabled: the web tool is advertised and the system prompt names it.
        {
            Harness h;
            h.settings.setAgentWebToolEnabled(true);
            h.script = { { { doneText(QStringLiteral("ok")) } } };
            auto vm = h.makeVm();
            vm->sendMessage(QStringLiteral("hi"));
            QTRY_VERIFY(!vm->busy());
            const InferenceRequest &req = h.provider->requests().first();
            QVERIFY(advertisesWebTool(req));
            QVERIFY(std::get<TextBlock>(req.messages.first().blocks.first())
                        .text.contains(QStringLiteral("read_online_plant_db")));
        }
    }

    // read_plant_photo is opt-in — advertised + named only when agentVisionEnabled is on.
    void visionToolOnlyRegisteredWhenEnabled()
    {
        auto advertisesPhotoTool = [](const InferenceRequest &req) {
            for (const ToolSpec &t : req.tools)
                if (t.name == QStringLiteral("read_plant_photo"))
                    return true;
            return false;
        };
        {
            Harness h;
            QVERIFY(!h.settings.agentVisionEnabled()); // off by default
            h.script = { { { doneText(QStringLiteral("ok")) } } };
            auto vm = h.makeVm();
            vm->sendMessage(QStringLiteral("hi"));
            QTRY_VERIFY(!vm->busy());
            const InferenceRequest &req = h.provider->requests().first();
            QVERIFY(!advertisesPhotoTool(req));
            QVERIFY(!std::get<TextBlock>(req.messages.first().blocks.first())
                         .text.contains(QStringLiteral("read_plant_photo")));
        }
        {
            Harness h;
            h.settings.setAgentVisionEnabled(true);
            h.script = { { { doneText(QStringLiteral("ok")) } } };
            auto vm = h.makeVm();
            vm->sendMessage(QStringLiteral("hi"));
            QTRY_VERIFY(!vm->busy());
            const InferenceRequest &req = h.provider->requests().first();
            QVERIFY(advertisesPhotoTool(req));
            QVERIFY(std::get<TextBlock>(req.messages.first().blocks.first())
                        .text.contains(QStringLiteral("read_plant_photo")));
        }
    }

    // A read_plant_photo result renders thumbnails (ImagesRole) and the photos show up in
    // outgoingImages (the pre-send disclosure surface).
    void visionPhotoRendersAndDiscloses()
    {
        Harness h;
        h.settings.setAgentVisionEnabled(true);
        const Plant p = makePlant();
        h.plants.add(p);
        JournalEntry e{ JournalEntryId::generate(), p.id, QDateTime::currentDateTimeUtc(),
                        JournalEntryKind::Observation, QStringLiteral("a leaf") };
        h.journal.add(e);
        const Attachment a{ AttachmentId::generate(), e.id, QStringLiteral("attachments/x.png"),
                            QString(), QDateTime::currentDateTimeUtc() };
        h.attachments.add(a);
        h.fileStore.put(a.fileRef, QByteArray("PNGBYTES"));

        h.script = {
            { { doneCall(QStringLiteral("c1"), QStringLiteral("read_plant_photo"),
                         QJsonObject{ { QStringLiteral("plant_id"), p.id.toString() } }) } },
            { { doneText(QStringLiteral("Looks healthy.")) } },
        };
        auto vm = h.makeVm();
        vm->sendMessage(QStringLiteral("what's wrong with my plant?"));
        QTRY_VERIFY(!vm->busy());

        // The tool-result row carries the photo as a data: URL (ImagesRole).
        bool sawImageRow = false;
        for (int i = 0; i < vm->rowCount(); ++i) {
            const QStringList imgs =
                vm->data(vm->index(i), AgentViewModel::ImagesRole).toStringList();
            if (rowKind(*vm, i) == int(AgentViewModel::ToolResultKind) && !imgs.isEmpty()) {
                sawImageRow = true;
                QVERIFY(imgs.first().startsWith(QStringLiteral("data:image/png;base64,")));
            }
        }
        QVERIFY(sawImageRow);
        // The disclosure surface lists the photo that left the device.
        QCOMPARE(vm->outgoingImages().size(), 1);
        QVERIFY(vm->outgoingImages().first().startsWith(QStringLiteral("data:image/png;base64,")));
    }

    void remoteEndpointSendsImmediatelyWithoutGate()
    {
        Harness h;
        h.settings.setAgentBaseUrl(QStringLiteral("https://api.example.com/v1"));
        h.script = { { { doneText(QStringLiteral("Hi.")) } } };
        auto vm = h.makeVm();

        // A remote endpoint is flagged for the inline notice, but sending is NOT gated — the
        // message goes straight through (no blocking pre-send disclosure dialog).
        QVERIFY(vm->endpointIsRemote());
        vm->sendMessage(QStringLiteral("hello"));
        QTRY_VERIFY(!vm->busy());
        QCOMPARE(vm->rowCount(), 2); // user + assistant, sent without any confirmation step
        QCOMPARE(h.persisted().size(), 2);
    }

    void localEndpointIsNotFlaggedRemote()
    {
        Harness h; // default endpoint is localhost Ollama
        auto vm = h.makeVm();
        QVERIFY(!vm->endpointIsRemote());
    }

    void reasoningStreamsRendersAndPersists()
    {
        Harness h;
        h.script = { { { ReasoningDelta{ QStringLiteral("the soil is dry") },
                         TextDelta{ QStringLiteral("Water it.") },
                         doneReasoning(QStringLiteral("the soil is dry"),
                                       QStringLiteral("Water it.")) } } };
        auto vm = h.makeVm();
        QSignalSpy reasoningSpy(vm.get(), &AgentViewModel::streamingReasoningChanged);

        vm->sendMessage(QStringLiteral("how is my basil?"));
        QTRY_VERIFY(!vm->busy());

        QVERIFY(reasoningSpy.count() > 0);            // reasoning streamed live
        QVERIFY(vm->streamingReasoning().isEmpty());  // cleared at turn end

        // Rows: user, reasoning (collapsed disclosure), assistant answer — in that order.
        QCOMPARE(vm->rowCount(), 3);
        QCOMPARE(rowKind(*vm, 1), int(AgentViewModel::ReasoningKind));
        QCOMPARE(rowText(*vm, 1), QStringLiteral("the soil is dry"));
        QCOMPARE(rowKind(*vm, 2), int(AgentViewModel::AssistantKind));
        QCOMPARE(rowText(*vm, 2), QStringLiteral("Water it."));

        // Reasoning round-trips through the transcript: a fresh view-model re-renders the row.
        h.script = {};
        auto reopened = h.makeVm();
        QCOMPARE(reopened->rowCount(), 3);
        QCOMPARE(rowKind(*reopened, 1), int(AgentViewModel::ReasoningKind));
        QCOMPARE(rowText(*reopened, 1), QStringLiteral("the soil is dry"));
    }

    void reasoningEffortReachesProvider()
    {
        Harness h;
        h.settings.setAgentReasoningEffort(2); // Medium
        h.script = { { { doneText(QStringLiteral("ok")) } } };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("hi"));
        QTRY_VERIFY(!vm->busy());

        QVERIFY(!h.provider->requests().isEmpty());
        QCOMPARE(h.provider->requests().last().reasoningEffort, ReasoningEffort::Medium);
    }

    void unitPreferencesReachPromptAndRebuildOnChange()
    {
        Harness h;
        h.script = { { { doneText(QStringLiteral("ok")) } } };
        auto vm = h.makeVm();

        vm->sendMessage(QStringLiteral("hi"));
        QTRY_VERIFY(!vm->busy());
        // The system prompt (request message 0) carries the default unit (°C).
        const Message sys = h.provider->requests().last().messages.first();
        QCOMPARE(sys.role, Role::System);
        QVERIFY(std::get<TextBlock>(sys.blocks.first()).text.contains(QStringLiteral("°C")));

        // Flipping the temperature unit rebuilds the session with a fresh prefix.
        h.settings.setTemperatureUnit(1); // Fahrenheit
        h.script = { { { doneText(QStringLiteral("ok2")) } } };
        vm->sendMessage(QStringLiteral("again"));
        QTRY_VERIFY(!vm->busy());
        QVERIFY(std::get<TextBlock>(h.provider->requests().last().messages.first().blocks.first())
                    .text.contains(QStringLiteral("°F")));
    }

    void resumesPersistedTranscriptOnConstruction()
    {
        Harness h;
        h.script = { { { doneText(QStringLiteral("Hello.")) } } };
        {
            auto vm = h.makeVm();
            vm->sendMessage(QStringLiteral("hi"));
            QTRY_VERIFY(!vm->busy());
            QCOMPARE(vm->rowCount(), 2);
        }
        // A fresh view-model over the same repositories reloads the transcript for display.
        h.script = {};
        auto reopened = h.makeVm();
        QCOMPARE(reopened->rowCount(), 2);
        QCOMPARE(rowText(*reopened, 0), QStringLiteral("hi"));
        QCOMPARE(rowText(*reopened, 1), QStringLiteral("Hello."));
    }
};

QTEST_GUILESS_MAIN(TestAgentViewModel)
#include "test_agentviewmodel.moc"
