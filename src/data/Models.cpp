#include "data/Models.hpp"

QJsonObject ProxyProfile::toJson() const {
    return {
        {"id", id},
        {"type", type},
        {"host", host},
        {"port", port},
        {"username", username},
        {"secretRef", secretRef},
    };
}

ProxyProfile ProxyProfile::fromJson(const QJsonObject& obj) {
    ProxyProfile p;
    p.id = obj.value("id").toString();
    p.type = obj.value("type").toString();
    p.host = obj.value("host").toString();
    p.port = obj.value("port").toInt();
    p.username = obj.value("username").toString();
    p.secretRef = obj.value("secretRef").toString();
    return p;
}

QJsonObject SessionProfile::toJson() const {
    QJsonArray tagArray;
    for (const auto& t : tags) {
        tagArray.append(t);
    }
    return {
        {"id", id},
        {"name", name},
        {"groupPath", groupPath},
        {"tags", tagArray},
        {"type", type},
        {"shellCommand", shellCommand},
        {"host", host},
        {"port", port},
        {"username", username},
        {"proxyRef", proxyRef},
        {"authMode", authMode},
        {"keyPath", keyPath},
        {"notes", notes},
    };
}

SessionProfile SessionProfile::fromJson(const QJsonObject& obj) {
    SessionProfile p;
    p.id = obj.value("id").toString();
    p.name = obj.value("name").toString();
    p.groupPath = obj.value("groupPath").toString();
    for (const auto& t : obj.value("tags").toArray()) {
        p.tags.append(t.toString());
    }
    p.type = obj.value("type").toString();
    p.shellCommand = obj.value("shellCommand").toString();
    p.host = obj.value("host").toString();
    p.port = obj.value("port").toInt(22);
    p.username = obj.value("username").toString();
    p.proxyRef = obj.value("proxyRef").toString();
    p.authMode = obj.value("authMode").toString();
    p.keyPath = obj.value("keyPath").toString();
    p.notes = obj.value("notes").toString();
    return p;
}

QJsonObject OllamaConfig::toJson() const {
    return {
        {"model", model},
        {"endpoint", endpoint},
    };
}

OllamaConfig OllamaConfig::fromJson(const QJsonObject& obj) {
    OllamaConfig c;
    c.model = obj.value("model").toString(c.model);
    c.endpoint = obj.value("endpoint").toString(c.endpoint);
    return c;
}

QJsonObject TerminalSettings::toJson() const {
    return {
        {"standardHistoryLines", standardHistoryLines},
        {"expandedHistoryEnabled", expandedHistoryEnabled},
        {"rightClickAutoCopy", rightClickAutoCopy},
        {"copyPasteMode", copyPasteMode},
    };
}

TerminalSettings TerminalSettings::fromJson(const QJsonObject& obj) {
    TerminalSettings s;
    s.standardHistoryLines = obj.value("standardHistoryLines").toInt(1000);
    s.expandedHistoryEnabled = obj.value("expandedHistoryEnabled").toBool(false);
    s.rightClickAutoCopy = obj.value("rightClickAutoCopy").toBool(true);
    s.copyPasteMode = obj.value("copyPasteMode").toString("platform");
    return s;
}

QJsonObject ScheduledWorkflow::toJson() const {
    return {
        {"name", name},
        {"timeHHMM", timeHHMM},
        {"profileQuery", profileQuery},
        {"scriptName", scriptName},
        {"enabled", enabled},
        {"lastRunDate", lastRunDate},
    };
}

ScheduledWorkflow ScheduledWorkflow::fromJson(const QJsonObject& obj) {
    ScheduledWorkflow w;
    w.name = obj.value("name").toString();
    w.timeHHMM = obj.value("timeHHMM").toString();
    w.profileQuery = obj.value("profileQuery").toString();
    w.scriptName = obj.value("scriptName").toString();
    w.enabled = obj.value("enabled").toBool(true);
    w.lastRunDate = obj.value("lastRunDate").toString();
    return w;
}

QJsonObject AppConfig::toJson() const {
    QJsonArray profileArray;
    for (const auto& p : profiles) {
        profileArray.append(p.toJson());
    }
    QJsonArray proxyArray;
    for (const auto& p : proxies) {
        proxyArray.append(p.toJson());
    }
    QJsonObject secretsObj;
    for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
        secretsObj.insert(it.key(), it.value());
    }
    QJsonObject scriptsObj;
    for (auto it = scripts.cbegin(); it != scripts.cend(); ++it) {
        scriptsObj.insert(it.key(), it.value());
    }
    QJsonArray workflowsArray;
    for (const auto& w : workflows) {
        workflowsArray.append(w.toJson());
    }
    return {
        {"version", version},
        {"profiles", profileArray},
        {"proxies", proxyArray},
        {"secrets", secretsObj},
        {"scripts", scriptsObj},
        {"workflows", workflowsArray},
        {"terminal", terminal.toJson()},
        {"uiState", uiState},
        {"ollama", ollama.toJson()},
    };
}

AppConfig AppConfig::fromJson(const QJsonObject& obj) {
    AppConfig c;
    c.version = obj.value("version").toInt(1);
    for (const auto& p : obj.value("profiles").toArray()) {
        c.profiles.append(SessionProfile::fromJson(p.toObject()));
    }
    for (const auto& p : obj.value("proxies").toArray()) {
        c.proxies.append(ProxyProfile::fromJson(p.toObject()));
    }
    const auto secretsObj = obj.value("secrets").toObject();
    for (auto it = secretsObj.begin(); it != secretsObj.end(); ++it) {
        c.secrets[it.key()] = it.value().toString();
    }
    const auto scriptsObj = obj.value("scripts").toObject();
    for (auto it = scriptsObj.begin(); it != scriptsObj.end(); ++it) {
        c.scripts[it.key()] = it.value().toString();
    }
    for (const auto& w : obj.value("workflows").toArray()) {
        c.workflows.append(ScheduledWorkflow::fromJson(w.toObject()));
    }
    c.terminal = TerminalSettings::fromJson(obj.value("terminal").toObject());
    c.uiState = obj.value("uiState").toString();
    c.ollama = OllamaConfig::fromJson(obj.value("ollama").toObject());
    return c;
}

namespace ModelJson {
QByteArray toBytes(const AppConfig& config) {
    return QJsonDocument(config.toJson()).toJson(QJsonDocument::Compact);
}

AppConfig fromBytes(const QByteArray& bytes) {
    const auto doc = QJsonDocument::fromJson(bytes);
    return AppConfig::fromJson(doc.object());
}
}
