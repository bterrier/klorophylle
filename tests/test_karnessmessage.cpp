// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "iprovider.h"
#include "message.h"
#include "messagecodec.h"

using namespace karness;

// Compile-time checks; also give the header-only seam files (iprovider.h,
// inferencerequest.h, toolspec.h, modelcaps.h) a compiling consumer.
static_assert(std::is_copy_constructible_v<Message>);
static_assert(std::is_copy_constructible_v<InferenceRequest>);
static_assert(std::has_virtual_destructor_v<IProvider>);

class TestKarnessMessage : public QObject {
    Q_OBJECT
private slots:
    void roundTripTextOnlyAllRoles_data()
    {
        QTest::addColumn<int>("role");
        QTest::newRow("system") << static_cast<int>(Role::System);
        QTest::newRow("user") << static_cast<int>(Role::User);
        QTest::newRow("assistant") << static_cast<int>(Role::Assistant);
        QTest::newRow("tool") << static_cast<int>(Role::Tool);
    }

    void roundTripTextOnlyAllRoles()
    {
        QFETCH(int, role);
        const Message original{static_cast<Role>(role),
                               {TextBlock{QStringLiteral("why are my leaves yellowing?")}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == original);
    }

    void roundTripReasoningOpaque()
    {
        const QJsonObject opaque{
            {QStringLiteral("signature"), QStringLiteral("EqMBCkgIBBABGAIiQB0v6m...")},
            {QStringLiteral("nested"), QJsonObject{{QStringLiteral("k"), 42}}},
        };
        const Message original{Role::Assistant,
                               {ReasoningBlock{QStringLiteral("the soil is dry"), opaque}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == original);
        // Opaque payload byte-for-byte (QJsonObject equality is key-order-insensitive).
        const auto &block = std::get<ReasoningBlock>(decoded->blocks.first());
        QCOMPARE(block.providerOpaque, opaque);
    }

    void roundTripToolCall()
    {
        const QJsonObject args{
            {QStringLiteral("plantId"), QStringLiteral("p-1")},
            {QStringLiteral("quantities"),
             QJsonArray{QStringLiteral("soilMoisture"), QStringLiteral("temperature")}},
            {QStringLiteral("windowDays"), 7},
        };
        const Message original{
            Role::Assistant,
            {ToolCallBlock{QStringLiteral("call_1"), QStringLiteral("read_plant_data"), args}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == original);
    }

    void roundTripToolResult()
    {
        const Message original{
            Role::Tool,
            {ToolResultBlock{QStringLiteral("call_1"),
                             {TextBlock{QStringLiteral("no sensor attached")}},
                             true}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == original);
        QVERIFY(std::get<ToolResultBlock>(decoded->blocks.first()).isError);
    }

    void roundTripToolResultImagePart()
    {
        const QByteArray bytes = QByteArrayLiteral("\x89PNG\r\n\x1a\n\x00\xff\xfe binary");
        const Message original{
            Role::Tool,
            {ToolResultBlock{QStringLiteral("call_2"),
                             {TextBlock{QStringLiteral("photo of 2026-06-01")},
                              ImageBlock{bytes, QStringLiteral("image/png")}},
                             false}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QVERIFY(*decoded == original);
    }

    void roundTripImageBlock()
    {
        QByteArray bytes;
        for (int i = 0; i < 256; ++i)
            bytes.append(static_cast<char>(i)); // every byte value, not valid UTF-8
        const Message original{Role::User, {ImageBlock{bytes, QStringLiteral("image/jpeg")}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QCOMPARE(std::get<ImageBlock>(decoded->blocks.first()).data, bytes);
        QVERIFY(*decoded == original);
    }

    void roundTripMultiBlockOrderPreserved()
    {
        const Message original{
            Role::Assistant,
            {TextBlock{QStringLiteral("checking…")},
             ReasoningBlock{QStringLiteral("need the history"), {}},
             ToolCallBlock{QStringLiteral("c1"), QStringLiteral("list_plants"), QJsonObject{}},
             ToolResultBlock{QStringLiteral("c1"), {TextBlock{QStringLiteral("Basil")}}, false},
             ImageBlock{QByteArrayLiteral("img"), QStringLiteral("image/png")}}};
        const auto decoded = messageFromJson(messageToJson(original));
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->blocks.size(), 5);
        QVERIFY(*decoded == original);
    }

    void fromJsonRejectsUnknownRole()
    {
        QJsonObject json = messageToJson(Message{Role::User, {TextBlock{QStringLiteral("x")}}});
        json.insert(QStringLiteral("role"), QStringLiteral("narrator"));
        const auto decoded = messageFromJson(json);
        QVERIFY(!decoded.has_value());
        QCOMPARE(decoded.error(), MessageCodecError::UnknownRole);
    }

    void fromJsonRejectsUnknownBlockType()
    {
        const QJsonObject json{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("blocks"),
             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("video")}}}}};
        const auto decoded = messageFromJson(json);
        QVERIFY(!decoded.has_value());
        QCOMPARE(decoded.error(), MessageCodecError::UnknownBlockType);
    }

    void fromJsonRejectsMissingField()
    {
        // tool_call without "id".
        const QJsonObject json{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("blocks"),
             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_call")},
                                    {QStringLiteral("name"), QStringLiteral("list_plants")},
                                    {QStringLiteral("args"), QJsonObject{}}}}}};
        const auto decoded = messageFromJson(json);
        QVERIFY(!decoded.has_value());
        QCOMPARE(decoded.error(), MessageCodecError::MissingField);
    }

    void fromJsonRejectsInvalidBase64()
    {
        const QJsonObject json{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("blocks"),
             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                    {QStringLiteral("mimeType"), QStringLiteral("image/png")},
                                    {QStringLiteral("data"), QStringLiteral("@@not base64@@")}}}}};
        const auto decoded = messageFromJson(json);
        QVERIFY(!decoded.has_value());
        QCOMPARE(decoded.error(), MessageCodecError::InvalidBase64);
    }
};

QTEST_GUILESS_MAIN(TestKarnessMessage)
#include "test_karnessmessage.moc"
