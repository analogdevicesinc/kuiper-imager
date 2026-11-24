// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Analog Devices, Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "kuiper/ipc/HelperProtocol.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>

#include <array>
#include <cstring>
#include <string_view>
#include <utility>

namespace kuiper::ipc {
namespace {

using Phase = Progress::Phase;

// Phase <-> wire string. A table keeps the two directions in sync.
constexpr std::array<std::pair<Phase, std::string_view>, 7> kPhases{{
    {Phase::Preparing, "Preparing"},
    {Phase::Downloading, "Downloading"},
    {Phase::Writing, "Writing"},
    {Phase::Finalizing, "Finalizing"},
    {Phase::Verifying, "Verifying"},
    {Phase::Configuring, "Configuring"},
    {Phase::Done, "Done"},
}};

std::string_view phaseName(Phase p) {
    for (const auto& [ph, name] : kPhases)
        if (ph == p) return name;
    return "Preparing";
}

Phase phaseFromName(const std::string& s) {
    for (const auto& [ph, name] : kPhases)
        if (s == name) return ph;
    return Phase::Preparing;
}

// ErrorCode <-> wire string. kuiper::toString() is the single source of the
// names (the encoder uses it); decoding just searches it, so this list only
// enumerates the codes and can't drift from the spelling.
constexpr std::array kCodes{
    ErrorCode::NetworkFailure,      ErrorCode::DiskFull,
    ErrorCode::PermissionDenied,    ErrorCode::InvalidImage,
    ErrorCode::HashMismatch,        ErrorCode::DeviceRemoved,
    ErrorCode::DeviceBusy,          ErrorCode::UnsupportedPlatform,
    ErrorCode::NotKuiper2,          ErrorCode::NotFound,
    ErrorCode::UserCancelled,       ErrorCode::Unknown,
};

ErrorCode codeFromName(const std::string& s) {
    for (const ErrorCode code : kCodes)
        if (s == toString(code)) return code;
    return ErrorCode::Unknown;
}

std::string compact(const QJsonObject& obj) {
    return QJsonDocument(obj)
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

QString qs(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

std::string field(const QJsonObject& o, const char* key) {
    return o.value(QLatin1String(key)).toString().toStdString();
}

}  // namespace

std::string encodeProgress(const Progress& p) {
    QJsonObject o;
    o["t"] = "progress";
    o["phase"] = qs(phaseName(p.phase));
    o["bytesDone"] = static_cast<double>(p.bytesDone);
    o["bytesTotal"] = static_cast<double>(p.bytesTotal);
    o["fraction"] = p.fraction;
    o["message"] = QString::fromStdString(p.message);
    return compact(o);
}

std::string encodeResult(const FlashSummary& summary) {
    QJsonObject o;
    o["t"] = "result";
    o["bytesWritten"] = static_cast<double>(summary.bytesWritten);
    o["sha256"] = QString::fromStdString(summary.sha256);
    return compact(o);
}

std::string encodeCustomizeResult(const CustomizeSummary& summary) {
    QJsonObject o;
    o["t"] = "customize-result";
    QJsonArray installed;
    for (const auto& id : summary.installed)
        installed.append(QString::fromStdString(id));
    o["installed"] = installed;
    o["arch"] = QString::fromStdString(summary.detectedArch);
    o["finalBytes"] = static_cast<double>(summary.finalImageBytes);
    o["ranScript"] = QString::fromStdString(summary.ranScript);
    return compact(o);
}

std::string encodeError(const Error& e) {
    QJsonObject o;
    o["t"] = "error";
    o["code"] = qs(std::string_view(toString(e.code)));
    o["message"] = QString::fromStdString(e.message);
    o["details"] = QString::fromStdString(e.details);
    if (e.recoverySuggestion)
        o["hint"] = QString::fromStdString(*e.recoverySuggestion);
    return compact(o);
}

HelperMessage parseLine(const std::string& line) {
    HelperMessage msg;
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(line));
    if (!doc.isObject()) return msg;  // Unknown: stray / non-JSON line
    const QJsonObject o = doc.object();
    const std::string t = field(o, "t");

    if (t == "progress") {
        msg.type = HelperMessage::Type::Progress;
        msg.progress.phase = phaseFromName(field(o, "phase"));
        msg.progress.bytesDone =
            static_cast<std::uint64_t>(o.value("bytesDone").toDouble());
        msg.progress.bytesTotal =
            static_cast<std::uint64_t>(o.value("bytesTotal").toDouble());
        msg.progress.fraction = o.value("fraction").toDouble();
        msg.progress.message = field(o, "message");
    } else if (t == "result") {
        msg.type = HelperMessage::Type::Result;
        msg.bytesWritten =
            static_cast<std::uint64_t>(o.value("bytesWritten").toDouble());
        msg.sha256 = field(o, "sha256");
    } else if (t == "customize-result") {
        msg.type = HelperMessage::Type::CustomizeResult;
        for (const QJsonValue& v : o.value("installed").toArray())
            msg.customize.installed.push_back(v.toString().toStdString());
        msg.customize.detectedArch = field(o, "arch");
        msg.customize.finalImageBytes =
            static_cast<std::uint64_t>(o.value("finalBytes").toDouble());
        msg.customize.ranScript = field(o, "ranScript");
    } else if (t == "error") {
        msg.type = HelperMessage::Type::Error;
        msg.error.code = codeFromName(field(o, "code"));
        msg.error.message = field(o, "message");
        msg.error.details = field(o, "details");
        if (o.contains("hint")) msg.error.recoverySuggestion = field(o, "hint");
    }
    return msg;
}

}  // namespace kuiper::ipc
